#include <ncurses.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define ARRAY_SIZE(x) ((sizeof x) / (sizeof *x))

const char* log_file ="x.log";

enum log_level{LOG_LEVEL_INFO, LOG_LEVEL_DEBUG};

struct log {
  enum log_level level;
  char* log_file;
  FILE* debug_file;
};

struct line {
  long line_number;
  int file_position;

  char* data;
  int data_len;
  struct line* next;
  struct line* prev;
  int line_pos;
};

struct search_state {
  int prev_line;
  int prev_col;
  char* str;
  
  /** Buffer searching */
  bool found;
  int line_number;
  int line_column;
  
  struct line* found_line;  
};

/**
 * Used to represent a particular buffer.
 */
struct buffer {
  off_t size;
  int num_lines;
  char* filepath;
  off_t fsize;
  char* buf_name;
  bool modified;

  struct line* current_line;
  struct line* lines;

  struct search_state* search;
};

/**
 * Used to represent a list of all open buffers.
 */
struct buffer_list {
  struct buffer_list* next;
  int num_buffers;
  struct buffer* cur;
};

enum display_mode { INSERT_MODE,COMMAND_MODE,SEARCH_MODE };

struct display{
  int height;
  int width;

  enum display_mode mode;

  struct line* start_line_ptr;
  struct buffer* current_buffer;

  int cursor_line;
  int cursor_column;
  WINDOW* mode_window;
  WINDOW* buffer_window;
};

struct log* XLOG;

#define LOG_DEBUG( ...)                            \
  do {                                          \
    if((XLOG)->level >=  LOG_LEVEL_DEBUG) {      \
      fprintf((XLOG)->debug_file,__VA_ARGS__);          \
      fflush((XLOG)->debug_file);                       \
    }                                           \
  } while(0);


struct log*
logging_init()
{
  struct log* log = malloc(sizeof(struct log));
  log->level = 0;
  #ifdef DEBUG
  log->level = LOG_LEVEL_DEBUG;
  log->debug_file = fopen("x-debug.log","w+");
  #endif
  XLOG = log;
  return log;
}

void
logging_end(struct log* log)
{
  #ifdef DEBUG
  fclose(log->debug_file);
  #endif
  free(log->log_file);
  free(log);
}


/**
 * Creates a line initialized with the copy of data;
 */
struct line*
line_create(char* data)
{
  struct line* newline = malloc(sizeof(struct line));
   newline->line_number = -1;
   newline->file_position = -1;
   long length = strlen(data);
   newline->data = malloc((sizeof(char)*length) +1) ;
   strncpy(newline->data,data,length);
   newline->data[length] ='\0';
   newline->data_len = length;
   return newline;
}

/**
 * Inserts a new line into list_head after the previous line.
 */
void
line_insert(struct line* new_line,
            struct line* prev_line,
            struct line** line_head)
{

  new_line->prev = NULL;
  new_line->next = NULL;

  if(prev_line != NULL) {
    LOG_DEBUG("Try int insert [%s] after [%s]\n",new_line->data,prev_line->data);

    new_line->prev = prev_line;

    struct line* old_next = prev_line->next;
    prev_line->next = new_line;
    new_line->next = old_next;

    if(NULL !=old_next) {
      old_next->prev = new_line;
    }

  } else{ // first line
    *line_head = new_line;
  }
}

/**
 * Merge the current line with the previous line. Due to the presence
 * of new line. When this line is merged with previous line we will
 * need to handle the newline.
 */
struct line*
line_merge(struct line* line,struct line** list_head)
{

  if(NULL == line->prev )
    return line;

  struct line* line_prev = line->prev;

  long line_length = strlen(line->data);
  long prev_length = strlen(line_prev->data);
  long joined_length = line_length + prev_length + 1;

  char* joined_lines = realloc(line_prev->data, joined_length);

  if(NULL == joined_lines )
    return line;

  line_prev->data     = strncat(joined_lines,line->data,line_length+prev_length);
  line_prev->data_len = strlen(joined_lines);

  line_prev->next = line->next;

  return line_prev;
}

struct line*
line_split(struct line* line, int split_pos, struct line** line_head)
{
  char* line_data = line->data;
  int line_len = strlen(line_data);

  if(split_pos >= line_len)
    return line;

  struct line* new_line = malloc(sizeof(struct line));

  int l2 = (line_len - split_pos)+1;

  new_line->data  = malloc(l2 * sizeof(char));
  strncpy(new_line->data,line->data+split_pos,l2);

  LOG_DEBUG( "line_split: new_line : %s" ,new_line->data);
  new_line->data_len = strlen(new_line->data);


  line->data = realloc(line->data,split_pos+2);
  line->data[split_pos]= '\n';
  line->data[split_pos+1] = '\0';

  LOG_DEBUG( "line_split: previous line : [%s]" ,line->data);
  LOG_DEBUG( "line_split: new      line : [%s]" ,new_line->data);


  line_insert(new_line,line,line_head);

  return new_line;
}

/**
 * Unlink the line from line list.
 */
struct line*
line_unlink(struct line* line, struct line** line_head)
{

  struct line* line_prev = line->prev;
  struct line* line_next = line->next;

  if( NULL == line_prev ) { // deleting first line
    *line_head = line_next;

    if(NULL != line_next) // no more previous line
      line_next->prev =  NULL;

  } else {     // deleting middle line

    line_prev->next = line->next;

    if(NULL != line_next) {
      line_next->prev = line_prev;
    }

  }
  return line;
}

static struct buffer_list* all_buffers;

struct buffer_list*
buffer_list_create()
{
  struct buffer_list* list =  malloc(sizeof(struct buffer_list));
  list->num_buffers = 0;
  list->cur = NULL;
  list->next = NULL;
  return list;
}

struct buffer*
buffer_alloc(char* buffer_name)
{
  struct buffer* buffer = malloc(sizeof(struct buffer));
  buffer->search = NULL;
  int len = strlen(buffer_name);
  buffer->buf_name = (char*) malloc((len+1)*sizeof(char));
  strncpy(buffer->buf_name,buffer_name,len);
  return buffer;
}

/**
 * Return a pointer to a line rather than using indices into content
 * field.
 */
struct line*
buffer_find_line(struct buffer* b, int num)
{

  if(num <= 0) { // go to begining of buffer
    num = 0;
  }

  if(num > b->num_lines) {
    num = b->num_lines-1;
  }

  int cnt = 0;

  struct line* cur = b->lines;
  while(cur!= NULL && cnt < num){
    cur = cur->next;
    cnt++;
  }
  return cur;
}

/**
 * Read file contents into lines.
 */
int
buffer_fill_lines(struct buffer* buffer,
                  const char* file_name)
{
  FILE* file =  fopen(file_name,"r");
  if(file == NULL) {
    return errno;
  }
  fseek(file,0,0);

  ssize_t read;
  size_t len = 0;
  char* line = NULL;

  long line_number = 0;
  long file_position = 0;

  buffer->lines  = NULL;
  buffer->modified = false;

  struct line* prev_line = NULL;

  while((read = getline(&line,&len,file)) != -1) {

    struct line* cur_line = malloc(sizeof(struct line));
    cur_line->prev = NULL;
    cur_line->next = NULL;

    if(prev_line!=NULL) {
      prev_line->next = cur_line;
      cur_line->prev = prev_line;
    }

    if(buffer->lines == NULL) { // first line
      buffer->lines = cur_line;
      prev_line = cur_line;
    }

    cur_line->line_number = line_number++;
    cur_line->file_position = file_position;
    file_position += (long) read;

    cur_line->data_len = strlen(line)+1;
    cur_line->data = line;

    prev_line = cur_line;
    line = NULL;
  }
  buffer->num_lines = line_number;
  fclose(file);
  return 0;
}



/**
 * A primitive search forward in the buffer.
 */
struct line*
buffer_search_forward(struct buffer* buffer)
{
  char* search = buffer->search->str;
  
  char* found  = NULL;
  int line_num = buffer->search->line_number;

  struct line* line = NULL;  
  for(line = buffer->lines; line != NULL; line = line->next,line_num++) {

    if(line_num < buffer->search->line_number)
      continue;

    if((found= strstr(line->data,search))!= NULL)
      break;
  }
  
  if(NULL == line) {
    LOG_DEBUG("not found!");
    buffer->search->found = false;
    buffer->search->prev_line = -1;
    buffer->search->prev_col = -1;
    return NULL;
  }

  buffer->search->prev_line = buffer->search->line_number;
  buffer->search->prev_col = buffer->search->line_column;
  buffer->search->line_number = line_num;
  buffer->search->line_column = found - line->data;
  buffer->search->found = true;
  
  LOG_DEBUG("Found \n[%s]\n at (line,column): (%d,%d)\n",
            line->data,
            buffer->search->line_number,
            buffer->search->line_column);
  
  return line;
}

/**
 * Keeping this fairly simple, just iterating through all the lines
 * and writing it to the file. The fill is trunkcated before writing
 * to it. Not very efficient for large files.
 */
void
buffer_save(struct buffer* buffer)
{

  if(NULL == buffer->filepath) {
    return;
  }

  FILE* savefile = fopen(buffer->filepath,"w+");

  if(NULL == savefile) {
    return;
  }

  struct line* line  = buffer->lines;
  while( NULL != line ) {
    // recompute to be sure of line-length
    long line_len  = strlen(line->data);
    if(line_len > 0) {
      fwrite(line->data,line_len,sizeof(char),savefile);
    }
    line = line->next;
  }
  fclose(savefile);
  buffer->modified = false;
}

/**
 * Opens a file given by file path
 */
struct buffer*
buffer_open_file(char* buffer_name, char* file_path)
{
  struct stat file_stat;

  if(0 == stat(file_path, &file_stat)) {
    off_t file_size = file_stat.st_size;
    struct buffer* buf = buffer_alloc(buffer_name);
    buf->fsize = file_size;
    buf->size = (long)file_size;
    int len = strlen(file_path);
    buf->filepath= malloc((len+1)*sizeof(char));
    strncpy(buf->filepath,file_path,len);
    buf->filepath[len]='\0';
    buffer_fill_lines(buf,file_path);

    return buf;
  } else{
    // perror
  }
  return NULL;
}

/**
 * Insert the character at postion speicfied in the current line
 */
void
buffer_insert_char(struct buffer* buffer,
                   char insert_char,
                   int insert_position)
{
  struct line* current_line = buffer->current_line;
  if(current_line == NULL || current_line->data == NULL ){
    return;
  }

  long len_line  = strlen(current_line->data);

  if(insert_position  > len_line || insert_position < 0) {
    return;
  }

  char * modified_line = realloc(buffer->current_line->data,(size_t) (len_line+2));

  if(modified_line == NULL) // data is left unmodified by realloc
    return;

  // shift modified right from the insert position we start from the
  // position of `\0 at len_line+1 and move and it to len_line

  int last_postion = len_line+1;
  int i = last_postion;
  for(;i > insert_position ; i--)
    modified_line[i]=modified_line[i-1];

  modified_line[insert_position] = insert_char;
  buffer->modified = true;
  buffer->current_line->data = modified_line;
  buffer->current_line->data_len++;
}


/**
 * Insert a new line into the buffer at the current line postion
 */
void
buffer_open_line(struct buffer* buffer)
{
   struct line * line  = buffer->current_line;

   if(NULL == line) {
    return;
   }

   struct line* newline = line_create("\n");
   line_insert(newline,line,&(buffer->lines));

   // new line will be the buffer current line
   buffer->current_line = newline;
}

char *
buffer_delete_current_line(struct buffer* buffer)
{
  struct line * line  = buffer->current_line;

  if(NULL == line) {
    return NULL;
  }

  line_unlink(line, &(buffer->lines));

  if(NULL != line->next) {
    buffer->current_line = line->next;
  } else { // go back to top if last line
    buffer->current_line = line->prev;
  }

  buffer->modified = true;
  // free line
  char* line_data = line->data;
  free(line);
  return line_data;
}

bool
buffer_split_line(struct buffer* buffer,
                  int split_postion)
{
  struct line* line  = buffer->current_line;

  if(NULL == line )
    return false;

  LOG_DEBUG( "buffer_split_line: position: %d\n",split_postion);

  char* line_data = line->data;
  int line_len = strlen(line_data);

  if(split_postion >= line_len)
    return false;

  buffer->current_line = line_split(buffer->current_line,split_postion,&buffer->lines);
  LOG_DEBUG("buffer_split_line: current line :%s \n",buffer->current_line->data);

  return true;

}

/**
 * Join the current line with the previous line.
 */
void
buffer_join_line(struct buffer* buffer)
{

  struct line * line  = buffer->current_line;

  if(NULL == line) {
    return;
  }

  struct line* line_prev = line->prev;

  if(NULL == line_prev) { // no previous line to join to
    return;
  }

  line_merge(line,&buffer->lines);
  buffer->current_line = line_prev;

  char* line_data = line->data;

  free(line);
  if(line_data)
    free(line_data);
}


/**
 * Delete the character at postion speicfied in the current line. Will
 * not overwrite the last newline character.
 */
void
buffer_delete_char(struct buffer* buffer,
                   int delete_position)
{
  struct line* current_line = buffer->current_line;
  if(current_line == NULL || current_line->data == NULL ){
    return;
  }

  long len_line  = strlen(current_line->data);
  if(delete_position  >= len_line-1 || delete_position < 0) {
    return;
  }

  int i = delete_position;
  char* line = current_line->data;
  for(; i < len_line-1; i++) {
    line[i] = line[i+1];
  }
  line[len_line-1]='\0';
  char * modified_line = realloc(buffer->current_line->data,(size_t) len_line);

  if(modified_line == NULL) // data is left unmodified by realloc
    return;

  buffer->modified = true;
  buffer->current_line->data = modified_line;
  buffer->current_line->data_len = strlen(buffer->current_line->data);
}

void
display_set_buffer(struct display* display,
                   struct buffer* buffer)
{
  display->current_buffer = buffer;
  display->current_buffer->current_line = display->start_line_ptr;
  display->start_line_ptr = buffer->lines;
  buffer->current_line = display->start_line_ptr;
}

void 
display_line_up(struct display * display) 
{
  if(display->current_buffer == NULL)
    return;

  struct buffer* current_buffer = display->current_buffer;

  if(current_buffer->current_line == NULL || current_buffer->current_line->prev == NULL){
    return;
  }
  current_buffer->current_line = current_buffer->current_line->prev;
}

bool
display_line_down(struct display * display)
{

  if(display->current_buffer == NULL)
    return false;

  struct buffer* current_buffer = display->current_buffer;

  if(current_buffer->current_line == NULL || current_buffer->current_line->next == NULL){
    return false;
  }

  current_buffer->current_line = current_buffer->current_line->next;

  if(display->cursor_column >= current_buffer->current_line->data_len - 1) {
    display->cursor_column = current_buffer->current_line->data_len - 2;
  }

  display->cursor_line +=1;
  move(display->cursor_line,display->cursor_column);

  return false;
}

/**
 * Starting with current line, return pointer to starting line of next
 * page, where page will be number of lines to keep on the page.
 */
bool
display_pg_down(struct display * display)
{
  // Using display height as page size
  long pg_size = display->height;
  int cur_line = 0;

  // assume we start at current current page.
  struct line* pg_start = display->start_line_ptr;
  struct line* cur = display->start_line_ptr;

  while(cur!= NULL && cur_line < pg_size){
    cur = cur->next;
    cur_line++;
  }

  if(cur!=NULL)
    pg_start = cur;

  display->start_line_ptr = pg_start;
  display->current_buffer->current_line = pg_start;

  display->cursor_line = 0;
  return true;
}

/**
 * Starting with current line, return pointer to starting line of next
 * page, where page will be number of lines to keep on the page.
 */
bool
display_pg_up(struct display * display)
{
  // Using display height as page size
  long pg_size = display->height;
  int cur_line = 0;

  // assume we start at current current page.
  struct line* pg_start = display->start_line_ptr;
  struct line* cur = display->start_line_ptr;

  while(cur!= NULL && cur_line < pg_size){
    cur = cur->prev;
    cur_line++;
  }

  if(cur!=NULL)  pg_start = cur;

  display->start_line_ptr = pg_start;
  display->current_buffer->current_line = pg_start;

  // set cursor back to top the screen
  display->cursor_line = 0;
  return true;
}


/**
 * return - Value will indicate whether full redisplay is required.
 */
bool
display_end_of_line(struct display* display)
{
  struct buffer* buffer = display->current_buffer;
  struct line* line = buffer->current_line;
  long line_len = strlen(line->data);
  display->cursor_column = line_len-1;
  wmove(display->buffer_window,display->cursor_line,display->cursor_column);
  wrefresh(display->buffer_window);
  move(display->cursor_line,display->cursor_column);
  return false;
}

bool
display_begining_of_line(struct display* display)
{
  display->cursor_column = 0;
  wmove(display->buffer_window,display->cursor_line,display->cursor_column);
  wrefresh(display->buffer_window);
  move(display->cursor_line,display->cursor_column);
  return false;
}

void
display_redraw(struct display* display)
{
  LOG_DEBUG("Called: display_redraw \n");

  if(display->buffer_window == NULL) {
    display->buffer_window = newwin(display->height,
                                    display->width,0,0);
  }

  wmove(display->buffer_window,0,0);
  wrefresh(display->buffer_window);

  int i = 0;
  struct line* start_line_ptr = display->start_line_ptr;
  while(start_line_ptr!=NULL && i < display->height) {
    wprintw(display->buffer_window,"%s",start_line_ptr->data);
    start_line_ptr = start_line_ptr->next;
    i++;
  }

  // fill rest of screen with blanks
  for(; i < display->height-1; i++) {
    wprintw(display->buffer_window,"~\n");
  }
}

bool
display_to_insert_mode(struct display* display)
{
  display->mode = INSERT_MODE;
  move(display->cursor_line,display->cursor_column);
  return false;
}

bool
display_to_command_mode(struct display* display)
{
  display->mode = COMMAND_MODE;
  move(display->cursor_line,display->cursor_column);
  return false;
}

bool
display_empty_linep(struct display* display)
{
  return strlen(display->current_buffer->current_line->data) >= 2;
}

/**
 * At the begining of the buffer's cursor.
 */
bool
display_cursor_bolp(struct display* display)
{
  return display->cursor_column <= 0;
}

bool
display_on_first_linep(struct display* display)
{
  struct buffer* buffer = display->current_buffer;
  return buffer->current_line == buffer->lines;
}

bool
display_on_last_linep(struct display* display)
{
  struct buffer* buffer = display->current_buffer;
  return buffer->current_line->next == NULL;
}

/**
 * Test that we are at the end of the current buffer's  line;
 */
bool
display_cursor_eolp(struct display* display)
{
  int last_postion = display->current_buffer->current_line->data_len;
  return display->cursor_column == last_postion;
}

bool
display_cursor_within_line(struct display* display)
{
  int last_postion = display->current_buffer->current_line->data_len;
  return display->cursor_column >=0  && display->cursor_column <= last_postion;
}

/**
 * Handle carriage return in insert mode.
 */
bool
display_insert_cr(struct display* display)
{
  LOG_DEBUG("called display_insert_cr line[%d] col:[%d] \n",
            display->cursor_line,display->cursor_column);
  
  if(buffer_split_line(display->current_buffer,display->cursor_column)) {
    display->cursor_column =0;
    display->cursor_line +=1;
    move(display->cursor_line,display->cursor_column);
  }
  
  LOG_DEBUG("display_insert_cr line [%d] col:[%d] \n",display->cursor_line,display->cursor_column);
  return true;
}

bool
display_startlinep(struct display* display)
{
  return display->start_line_ptr == display->current_buffer->current_line;
}

/**
 * Moves cursor to a particular line or column in the buffer. If the
 * line lies out of current sceen. Scrolls the buffer so that the
 * matching line shows on the last line of column 
 */
bool
display_goto_position(struct display* display, int nline, int column)
{
  struct line* line = display->current_buffer->lines;
  struct line* start = line;

  int n = 0;
  int pos = 0;

  while(n < nline && NULL != line ) {
    if(pos > display->height) {
      pos = 0;
      start = line;  // update what will be screen start 
    }
    line = line->next;
    n++;
  }

  if(NULL == line) {
    LOG_DEBUG("display_goto_position: (%d,%d) is out of bounds, max :%d",
              nline,column,n);
    return false;
  }
  
  LOG_DEBUG("display_goto_position (%d,%d)",nline,column);
  
  display->start_line_ptr = start;
  display->cursor_line = nline;
  display->cursor_column = column;
  display->current_buffer->current_line = line;
  wmove(display->buffer_window,display->cursor_line,display->cursor_column);
  wrefresh(display->buffer_window);
  
  return true;
}

/**
 * Handle backspace or delete key
 */
bool
display_insert_backspace(struct display* display)
{
  bool redisplay = true;

  if(!display_cursor_bolp(display)) { // if not begining of the line
    buffer_delete_char(display->current_buffer, --(display->cursor_column));
  } else { // We are at the begining of the line
    if(!display_startlinep(display)) {
      buffer_join_line(display->current_buffer);
      display->cursor_line--;
      display->cursor_column = strlen(display->current_buffer->current_line->data)-1;
      LOG_DEBUG("buffer_join_line result [%s] cursor_line %d, cursor_column %d ",
                display->current_buffer->current_line->data,
                display->cursor_column,
                display->cursor_line);
      move(display->cursor_line,display->cursor_column);
      return true;
    }

    if(display_empty_linep(display)) {
      move(display->cursor_line,display->cursor_column);
      return false;
    }

    // move dispaly off the current line
    if(display_startlinep(display)) {

      if(NULL != display->current_buffer->current_line->next) {
        display->start_line_ptr = display->current_buffer->current_line->next;
      } else {
        display->start_line_ptr = display->current_buffer->current_line->prev;
      }
    }

    char * line_data = buffer_delete_current_line(display->current_buffer);

    if(line_data)
      free(line_data);

    redisplay =true;

  }

  return redisplay;
}


/**
 * Clear the existing mode line. 
 */
void
mode_line_clear(struct display* display)
{
  wmove(display->mode_window,0,0);
  LOG_DEBUG("display-width :%d\n", display->width);
  int i =0;
  for(i = 0; i  <  display->width - 1; i++) {
    wprintw(display->mode_window," ");
  }
  wmove(display->mode_window,0,0);
  wrefresh(display->mode_window);
}

/**
 * Get user input commands
 */
char*
mode_line_input(char* prompt,
                struct display* display)
{
  mode_line_clear(display);  
  wprintw(display->mode_window,"%s",prompt);
  wrefresh(display->mode_window);

  char user_input[1024];

  move(display->height, 1);
  echo();
  getstr(user_input);
  noecho();

  int len = strlen(user_input);
  char* retval = malloc((len+1)*sizeof(char));
  strncpy(retval,user_input,len);
  retval[len] = '\0';

  return retval;
}

/**
 * Show the mode-line at the bottom of the display.
 */
void
mode_line_show(struct display* display)
{
  struct buffer* cur = display->current_buffer;

  char mode_line[display->width-1];
  snprintf(mode_line,display->width-1,
           "-[%s name: %s, cursor:(%d,%d) len:%d num_lines:%d , mode:%s ][%d x %d] line:%s",
           cur->modified ? "**" : " ",
           cur->buf_name,
           display->cursor_line,
           display->cursor_column,
           (int)strlen(cur->current_line->data),
           cur->num_lines,
           display->mode == INSERT_MODE ? "---INSERT---" : "---NAVIGATE----",
           display->height,
           display->width,
           display->current_buffer->current_line->data);
  
  wmove(display->mode_window,0,0);
  wprintw(display->mode_window,mode_line);

 int i  = strlen(mode_line);
 for(; i < display->width; i++)
   wprintw(display->mode_window,"%c", '-');
 wrefresh(display->mode_window);
}

struct display*
display_init(struct buffer* buffer,int height,int width)
{
  struct display* dis = malloc(sizeof(struct display));
  dis->cursor_column = 0;
  dis->cursor_line = 0;
  dis->height = height;
  dis->width = width;
  dis->mode_window   = newwin(height-1,width,height-2,0);
  dis->buffer_window = newwin(height-2,width,0,0);
  dis->mode = COMMAND_MODE;
  display_set_buffer(dis, buffer);
  return dis;
}

void
start_display(struct buffer* buffer)
{
  char cur;
  int height, width;
  
  initscr();
  getmaxyx(stdscr, height, width);

  struct display* display = display_init(buffer,height,width);

  // leave space for mode line
  display->height -= 2;

  raw();
  refresh();
  bool redisplay = false;
  bool quit = false;

  while(!quit) {

    noecho();

    move(display->cursor_line,display->cursor_column);

    display_redraw(display);

    wrefresh(display->mode_window);
    wrefresh(display->buffer_window);

    redisplay = false;

    while(!redisplay) {

      mode_line_show(display);
      cur = getch();

      LOG_DEBUG("display_loop: received [%c] \n",cur);

      // TODO need some way to nest modes
      // creae indexes commands and key-maps
      
      if (display->mode == INSERT_MODE) {
        //if(strcmp("^R", key_name(cur)) == 0){
        //LOG_DEBUG("display_loop : Detected Ctrl-R");
          //        }else
        if(3 == cur) { // Ctrl-C go back ot view mode.
          redisplay = display_to_command_mode(display);
        } else if(KEY_ENTER == cur || '\n' == cur ) {
          LOG_DEBUG("display_loop: Detected Enter in INSERT_MODE\n");
           redisplay = display_insert_cr(display);
        } else if (KEY_BACKSPACE == cur  || 127 == cur || 8 == cur || cur == '\b') { // backspace or delete
          redisplay = display_insert_backspace(display);
        } else if (10 == cur) { // Open a line
          buffer_open_line(display->current_buffer);
          move(display->cursor_line++,display->cursor_column);
          redisplay = true;
        } else {
          buffer_insert_char(display->current_buffer, cur, display->cursor_column++);
          move(display->cursor_line,display->cursor_column);
          redisplay = true;
        }
        // Navigation Commands
      } if (display->mode == SEARCH_MODE ){
         if (3 == cur || 'q' == cur) { // search quit
           display->mode = COMMAND_MODE;
         } else if ('n' == cur) { // search forward
           
         } else if ('N' == cur) { // search backwards
           
         } else { // unrecognized.
           
         }
         
      } else if (3 == cur || 'q' == cur) { // quit
        quit = true;
        break;
      } else if (cur == '<') {
        display->cursor_column = 0;
        redisplay = display_pg_up(display);
      } else if (cur == '>'){
        display->cursor_column = 0;
        redisplay = display_pg_down(display);
      } else if ('j' == cur || KEY_DOWN == cur) {
        if(display_on_last_linep(display)) {
          move(display->cursor_line,display->cursor_column);
          continue;
        }
        if(display->cursor_line+1 >= display->height)  {
          display->cursor_line =0;
          redisplay = true;
          display_pg_down(display);
        } else {
          display_line_down(display);
        }
      } else if ('k' == cur || KEY_UP == cur) {
        if(display_on_first_linep(display)) {
          move(display->cursor_line,display->cursor_column);
          continue;
        }
        if(display->cursor_line - 1 < 0){
          display_pg_up(display);
          display->cursor_line = display->height;
          redisplay = true;
        } else {
          display_line_up(display);
          move(--(display->cursor_line),display->cursor_column);
        }
      } else if ('l' == cur || KEY_RIGHT == cur) {
         if(display->cursor_column < display->width &&
            display->cursor_column < display->current_buffer->current_line->data_len -1) {
           move(display->cursor_line,display->cursor_column++);
         } else {
           display->cursor_column = display->current_buffer->current_line->data_len;
           move(display->cursor_line,display->cursor_column);
         }
      } else if ('h' == cur || KEY_LEFT == cur) {
        if(display->cursor_column > 0) {
          move(display->cursor_line,--(display->cursor_column));
        } else{
          display->cursor_column = 0;
          move(display->cursor_line,0);
        }
      } else if ('x' == cur) {
        buffer_delete_char(display->current_buffer,display->cursor_column);
        redisplay = true;
      } else if ('$' == cur) {
        display_end_of_line(display);
      } else if ('^' == cur) {
        display_begining_of_line(display);
      } else if ('d' == cur) {
        if(display->start_line_ptr == display->current_buffer->current_line) {
          // move dispaly off the curreont line
          if(NULL != display->current_buffer->current_line->next) {
            display->start_line_ptr = display->current_buffer->current_line->next;
          } else {
            display->start_line_ptr = display->current_buffer->current_line->prev;
          }
        }
        // now drop current line
        buffer_delete_current_line(display->current_buffer);
        redisplay = true;
      } else if ('i' == cur) {
        redisplay = display_to_insert_mode(display);
        move(display->cursor_line,display->cursor_column);
      } else if ('o' == cur) {
        buffer_open_line(display->current_buffer);
        move(display->cursor_line++,display->cursor_column);
        redisplay = true;
      } else if ('J' == cur) {
        buffer_join_line(display->current_buffer);
        move(display->cursor_line,display->cursor_column);
        redisplay = true;
      } else  if ('s' == cur) {
        buffer_save(display->current_buffer);
        move(display->cursor_line,display->cursor_column);
      }  else if(':' == cur) { // read command
        // TODO Write Command Reader

        char* search_term = mode_line_input(":",display);
        if(search_term == NULL) {
          redisplay = true;
          continue;
        }
        LOG_DEBUG("read cmd: %s ",search_term);
      } else if( '/' == cur || '?' == cur ) { // search command
        
        char* search_term = mode_line_input("/",display);

        if(search_term == NULL) {
          redisplay = true;
          continue;
        }
        
        LOG_DEBUG("Search for: %s ",search_term);

        struct buffer *buffer = display->current_buffer;
        
        if(NULL != buffer->search) {
          free(buffer->search->str);
          free(buffer->search);
        }

        buffer->search = malloc(sizeof(struct search_state));


        // search from beginning for now
        buffer->search->prev_line = 0;
        buffer->search->prev_col = 0;
        buffer->search->str = malloc(sizeof(search_term));
        strncpy(buffer->search->str,search_term,sizeof(search_term));

        redisplay = buffer_search_forward(display->current_buffer);
        
        if(display->current_buffer->search->found)  { // found search
          struct search_state* search =  display->current_buffer->search;
          redisplay = display_goto_position(display, search->line_number, search->line_column);
          move(display->cursor_line,display->cursor_column);
        }

      } else {
        // ignore unknown commands
        move(display->cursor_line,display->cursor_column);
      }
    }
  }
  endwin();
}


void run_tests();

int
main(int argc,
     char* argv[])
{

  XLOG = logging_init();

  run_tests();

  all_buffers = buffer_list_create();
  if(argc > 1)
    all_buffers->cur = buffer_open_file("x.c", argv[1]);
  else
    all_buffers->cur = buffer_open_file("x.c", "/home/aakarsh/src/c/x/x.c");

  if(all_buffers->cur){
    start_display(all_buffers->cur);
  }

  logging_end(XLOG);

  return 0;
}

void test_line_split() {
 struct line* l = line_create("hello world");
  l = line_split(l,6,NULL);
  assert(strcmp("world",l->data) == 0);

}

void run_tests() {
 test_line_split();
}

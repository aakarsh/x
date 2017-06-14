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

struct line {
  // these values are for unmodified buffers lines
  long line_number;
  int file_position;

  char * data;
  int data_len;
  struct line* next;
  struct line* prev;
};

struct buffer_region {
  off_t size;
  int num_lines;
  char* filepath;
  off_t fsize;
  char* buf_name;
  bool modified;
  
  struct line* current_line;
  struct line* lines;
};

struct buffer_list {
  int num_buffers;
  struct buffer_region* cur;
  struct buffer_list* next;
};

static struct buffer_list* all_buffers;

struct buffer_list* buffer_list_create() {
  struct buffer_list* list =  malloc(sizeof(struct buffer_list));
  list->num_buffers = 0;
  list->cur = NULL;
  list->next = NULL;
  return list;
}

struct buffer_region* buffer_alloc(char* buffer_name) {
  struct buffer_region* buffer = malloc(sizeof(struct buffer_region));
  int len = strlen(buffer_name);
  buffer->buf_name = (char*) malloc((len+1)*sizeof(char));
  strncpy(buffer->buf_name,buffer_name,len);
  return buffer;
}

/**
 * Return a pointer to a line rather than using indices into content
 * field.
 */
struct line* buffer_find_line(struct buffer_region* buffer, int line_number) {

  if(line_number <= 0) { // go to begining of buffer
    line_number = 0;
  }

  if(line_number > buffer->num_lines) {
    line_number = buffer->num_lines-1;
  }

  int cur_line = 0;

  struct line* cur = buffer->lines;
  while(cur!= NULL && cur_line < line_number){
    cur = cur->next;
    cur_line++;
  }
  return cur;
}

/**
 * Read file contents into lines.
 */
int buffer_fill_lines(struct buffer_region* buffer, const char* file_name) {
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
 * Keeping this fairly simple, just iterating through all the lines
 * and writing it to the file. The fill is trunkcated before writing
 * to it. Not very efficient for large files.
 */
void buffer_save(struct buffer_region* buffer) {
  
  if(NULL == buffer->filepath) {
    return;
  }
  
  FILE* savefile = fopen(buffer->filepath,"w+");
  
  if(NULL == savefile) {
    return;
  }
  
  struct line* line  = buffer->lines;  
  while( NULL != line ) {
    // recompute to be sure of line length
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
struct buffer_region* buffer_open_file(char* buffer_name, char* file_path) {
  struct stat file_stat;

  if(0 == stat(file_path, &file_stat)) {
    off_t file_size = file_stat.st_size;
    struct buffer_region* buf = buffer_alloc(buffer_name);
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
void buffer_insert_char(struct buffer_region* buffer,char insert_char, int insert_position) {

  struct line* current_line = buffer->current_line;
  if(current_line == NULL || current_line->data == NULL ){
    return;
  }

  long len_line  = strlen(current_line->data);

  if(insert_position  >= (len_line-1) || insert_position < 0) {
    return;
  }

  char * modified_line = realloc(buffer->current_line->data,(size_t) (len_line+2));

  if(modified_line == NULL) // data is left unmodified by realloc
    return;

  // shift modified right from the insert position
  // we start from the position of `\0 at len_line+1 and move and it to len_line
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
 * Insert a new line into the buffer at the current line postion.
 */
void buffer_open_line(struct buffer_region* buffer) {
   struct line * line  = buffer->current_line;   
   if(NULL == line) {
    return;
   }   

   struct line* newline = malloc(sizeof(struct line));
   newline->line_number = -1;
   newline->file_position = -1;   

   newline->data = malloc(2*sizeof(char));
   strncpy(newline->data,"\n",2);
   newline->data_len = 1;
   
   struct line* line_next = line->next;
   line->next = newline;
   newline->next = line_next;
   newline->prev = line;
   
   // new line will be the buffer current line
   buffer->current_line = newline;
   
}

char * buffer_delete_current_line(struct buffer_region* buffer) {
  struct line * line  = buffer->current_line;
  
  if(NULL == line) {
    return NULL;
  }

  struct line* line_prev = line->prev;
  struct line* line_next = line->next;
  
  if( NULL == line_prev ) { // deleting first line
    buffer->lines = line_next;    
  } else { // deleting middle line
    line_prev->next = line_next;    
  }
  
  if(NULL != line_next) {
      line_next->prev = line_prev;
      buffer->current_line = line_next;
  } else{
    buffer->current_line = line_prev;
  }
  buffer->modified = true;
  // free line
  char* line_data = line->data;
  free(line);
  
  return line_data;

}

/**
 * Join the current line with the previous line. 
 */
void buffer_join_line(struct buffer_region* buffer) {
  
  struct line * line  = buffer->current_line;
  
  if(NULL == line) {
    return;
  }
  struct line* line_prev = line->prev;
  if(NULL == line_prev) { // no previous line to join to 
    return;
  }  
  // append current line to previous line's data
  long line_length = strlen(line->data);
  long prev_length = strlen(line_prev->data);
  long joined_length = line_length+prev_length+1;

  char* joined_lines = realloc(line_prev->data, joined_length);
  if(NULL == joined_lines )
    return;  
  line_prev->data  = strncat(joined_lines,line->data,joined_length);
  line_prev->data_len = strlen(joined_lines);
    
  char* line_data = buffer_delete_current_line(buffer);
  buffer->current_line = line_prev;
  if(line_data)
    free(line_data);
  
}


/**
 * Delete the character at postion speicfied in the current line. Will
 * not overwrite the last newline character.
 */
void buffer_delete_char(struct buffer_region* buffer, int delete_position) {
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
  buffer->current_line->data_len--;
}

struct buffer_display{
  int height;
  int width;

  bool insert_mode;
  struct line* start_line_ptr;
  struct buffer_region* current_buffer;


  int cursor_line;
  int cursor_column;
  WINDOW* mode_window;
  WINDOW* buffer_window;
};

void display_set_buffer(struct buffer_display* display, struct buffer_region* buffer) {
  display->current_buffer = buffer;
  display->current_buffer->current_line = display->start_line_ptr;
  display->start_line_ptr = buffer->lines;
  buffer->current_line = display->start_line_ptr;
}

inline void display_line_up(struct buffer_display * display) {
  if(display->current_buffer == NULL)
    return;

  struct buffer_region* current_buffer = display->current_buffer;

  if(current_buffer->current_line == NULL || current_buffer->current_line->prev == NULL){
    return;
  }

  current_buffer->current_line = current_buffer->current_line->prev;
}

inline void display_line_down(struct buffer_display * display) {
  if(display->current_buffer == NULL)
    return;

  struct buffer_region* current_buffer = display->current_buffer;

  if(current_buffer->current_line == NULL || current_buffer->current_line->next == NULL){
    return;
  }
  current_buffer->current_line = current_buffer->current_line->next;
}


/**
 * Starting with current line, return pointer to starting line of next
 * page, where page will be number of lines to keep on the page.
 */
bool display_pg_down(struct buffer_display * display)
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
bool display_pg_up(struct buffer_display * display)
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
bool display_end_of_line(struct buffer_display* display)
{
  struct buffer_region* buffer = display->current_buffer;
  struct line* line = buffer->current_line;
  long line_len = strlen(line->data);
  display->cursor_column = line_len-2;
  wmove(display->buffer_window,display->cursor_line,display->cursor_column);
  wrefresh(display->buffer_window);
  move(display->cursor_line,display->cursor_column);
  return false;
}

bool display_begining_of_line(struct buffer_display* display)
{
  display->cursor_column = 0;
  wmove(display->buffer_window,display->cursor_line,display->cursor_column);
  wrefresh(display->buffer_window);
  move(display->cursor_line,display->cursor_column);
  return false;
}
  
void display_redraw(struct buffer_display* display) {
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

/**
 * Show the mode-line at the bottom of the display.
 */
void mode_line_show(struct buffer_display* display) {
  if(display->mode_window == NULL)
    display->mode_window = newwin(display->height+1,display->width,display->height,0);

  wmove(display->mode_window,0,0);
  struct buffer_region* cur = display->current_buffer;

  char mode_line[1024];
  snprintf(mode_line,1024,"-[%s name: %s, cursor:(%d,%d) len:%d num_lines:%d , mode:%s ][%d x %d]",
           cur->modified ? "**" : " ",
           cur->buf_name,
           display->cursor_line,
           display->cursor_column,
           (int)strlen(cur->current_line->data),
           cur->num_lines,
           display->insert_mode ? "---INSERT---" : "---NAVIGATE----",
           display->height,
           display->width);

  wprintw(display->mode_window,mode_line);

 int i  = strlen(mode_line);
 for(; i < display->width; i++)
   wprintw(display->mode_window,"%c", '-');
 wrefresh(display->mode_window);
}

void display_loop() {

  struct buffer_display* display = malloc(sizeof(struct buffer_display));


  display->cursor_column = 0;
  display->cursor_line = 0;
  display->mode_window = NULL;
  display->buffer_window = NULL;
  display->insert_mode = false;

  display_set_buffer(display,all_buffers->cur);


  char cur ;
  initscr();
  getmaxyx(stdscr, display->height, display->width);
  // leave space for mode line
  display->height-=2;

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
      
      if (display->insert_mode) {
        if(3 == cur) { // Ctrl-C go back ot view mode.
          display->insert_mode = false;
          move(display->cursor_line,display->cursor_column);
        } else if(KEY_ENTER == cur) {
          buffer_open_line(display->current_buffer);
          display->cursor_column =0;
          move(++display->cursor_line,display->cursor_column);
          redisplay = true;
        } else if (KEY_BACKSPACE == cur  || 127 == cur || 8 == cur || cur == '\b') { // backspace or delete
          if(display->cursor_column > 0) {
            buffer_delete_char(display->current_buffer, --(display->cursor_column));
          } else {
            if(strlen(display->current_buffer->current_line->data) >= 2) {
              move(display->cursor_line,display->cursor_column);
              continue;
            }
            if(display->start_line_ptr == display->current_buffer->current_line) {
              // move dispaly off the curreont line
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
          redisplay = true;
        } else if (10 == cur) {
          buffer_open_line(display->current_buffer);
          move(display->cursor_line++,display->cursor_column);
          redisplay = true;
        } else {
          buffer_insert_char(display->current_buffer, cur, display->cursor_column++);
          move(display->cursor_line,display->cursor_column);
          redisplay = true;
        }        
        // Navigation Commands
      } else if (3 == cur || 'q' == cur) { // quit
        quit = true;
        break;
      } else if (cur == '<') {
        redisplay = display_pg_up(display);
      } else if (cur == '>'){
        redisplay = display_pg_down(display);
      } else if ('j' == cur) {
        if(display->cursor_line+1 >= display->height)  {
          display->cursor_line =0;
          redisplay = true;
          display_pg_down(display);
        } else {
          display_line_down(display);
          move(++display->cursor_line,display->cursor_column);
        }
      } else if ('k' == cur) {
        if(display->cursor_line - 1 < 0){
          display_pg_up(display);
          display->cursor_line = display->height;
          redisplay = true;
        } else {
          display_line_up(display);
          move(--(display->cursor_line),display->cursor_column);
        }
      } else if ('l' == cur) {
         if(display->cursor_column < display->width &&
            display->cursor_column < display->current_buffer->current_line->data_len) {
           move(display->cursor_line,(display->cursor_column)++);
         } else {
           display->cursor_column = display->current_buffer->current_line->data_len;
           move(display->cursor_line,display->cursor_column);
         }
      } else if ('h' == cur) {
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
        display->insert_mode = true;
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
      } else {
        // ignore unknown commands
        move(display->cursor_line,display->cursor_column);
      }
    }
  }
  endwin();
}

int main(int argc, char* argv[]){
  all_buffers = buffer_list_create();
  if(argc > 1)
    all_buffers->cur = buffer_open_file("x.c", argv[1]);
  else
    all_buffers->cur = buffer_open_file("x.c", "/home/aakarsh/src/c/x/x.c");

  if(all_buffers->cur){
    display_loop();
  }
  return 0;
}


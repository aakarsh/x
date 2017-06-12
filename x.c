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
static  int DISPLAY_SIZE = 512;
static  int DEFAULT_BUFFER_SIZE = 8192;

struct line {
  // These values are for unmodified buffers lines
  long line_number;
  int file_position;

  char * data;
  int data_len;

  bool modified;
  struct line* next;
  struct line* prev;
};

struct buffer_region {
  off_t size;
  int end_pos;
  int cur_pos;
  int num_lines;
  char* filepath;
  off_t fsize;
  char* buf_name;

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

struct buffer_region* buffer_alloc(off_t size,char* buffer_name) {

  struct buffer_region* buffer = malloc(sizeof(struct buffer_region));

  buffer->size = size;
  buffer->end_pos = 0;
  buffer->cur_pos  = 0;
  
  int len = strlen(buffer_name);
  buffer->buf_name = (char*) malloc((len+1)*sizeof(char));
  strncpy(buffer->buf_name,buffer_name,len);


  return buffer;
}

inline void buffer_rewind(struct buffer_region* buffer){
  buffer->cur_pos = 0;
}

inline void buffer_set_point(struct buffer_region* buffer , int point){
  if(point < buffer->end_pos)
    buffer->cur_pos = point;
}


/**
 * Return a pointer to a line rather than using indices into content
 * field.
 */
inline struct line* buffer_find_line(struct buffer_region* buffer, int line_number) {

  if(line_number <= 0) { // go to begining of buffer
    line_number = 0;
  }

  // TODO: we will modify the number of lines
  if(line_number > buffer->num_lines) {
    line_number = buffer->num_lines-1;
  }

  int i = 0;
  int cur_line = 0;
  int line_start_pos = 0;

  struct line* cur = buffer->lines;
  while(cur!= NULL && cur_line < line_number){
    cur = cur->next;
    cur_line++;
  }

  return cur;
}


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
  struct line* prev_line = NULL;

  while((read = getline(&line,&len,file)) != -1) {

    struct line* cur_line = malloc(sizeof(struct line));
    cur_line->prev = NULL;
    cur_line->next = NULL;
    cur_line->modified = false;


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

  fclose(file);
  return 0;
}

/**
 * Opens a file given by file path
 */
struct buffer_region* buffer_open_file(char* buffer_name, char* file_path) {
  struct stat file_stat;

  if(0 == stat(file_path, &file_stat)) {
    off_t file_size = file_stat.st_size;
    struct buffer_region* buf = buffer_alloc(file_size,buffer_name);
    buf->fsize = file_size;
    int len = strlen(file_path);
    buf->filepath= malloc((len+1)*sizeof(char));
    strncpy(buf->filepath,file_path,len);
    buf->filepath[len]='\0';
    //    buffer_fill(buf,file_path,0);
    buffer_fill_lines(buf,file_path);


    return buf;
  } else{
    // perror
  }
  return NULL;
}

struct buffer_display{
  int height;
  int width;
  struct line* start_line_ptr;
  struct line* current_line_ptr;
  int cursor_line;
  int cursor_column;
  WINDOW* mode_window;
  WINDOW* buffer_window;
};

inline void display_line_up(struct buffer_display * display) {
  if(display->current_line_ptr == NULL || display->current_line_ptr->prev == NULL){
    return;
  }
  display->current_line_ptr = display->current_line_ptr->prev;
}

inline void display_line_down(struct buffer_display * display) {
  if(display->current_line_ptr == NULL || display->current_line_ptr->next == NULL){
    return;
  }
  display->current_line_ptr = display->current_line_ptr->next;
}

/**
 * Starting with current line, return pointer to starting line of next
 * page, where page will be number of lines to keep on the page.
 */
inline void display_pg_down(struct buffer_display * display)
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
}


/**
 * Starting with current line, return pointer to starting line of next
 * page, where page will be number of lines to keep on the page.
 */
inline void display_pg_up(struct buffer_display * display)
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

  if(cur!=NULL)
    pg_start = cur;

  display->start_line_ptr = pg_start;
}

void buffer_show(struct buffer_display* display) {
  if(display->buffer_window == NULL) {
    display->buffer_window = newwin(display->height,
                                    display->width,0,0);
  }

  struct buffer_region* buf = all_buffers->cur;

  wmove(display->buffer_window,0,0);
  wrefresh(display->buffer_window);

  int i = 0;
  struct line* start_line_ptr = display->start_line_ptr;
  while(start_line_ptr!=NULL && i < display->height) {
    wprintw(display->buffer_window,"%s",start_line_ptr->data);
    start_line_ptr = start_line_ptr->next;
    i++;
  }

  // Fill rest of screen with blanks
  for(; i <display->height; i++) {
    wprintw(display->buffer_window,"\n");
  }

}


/**
 * Show the mode-line at the bottom of the display.
 */
void mode_line_show(struct buffer_display* display) {
  if(display->mode_window == NULL)
    display->mode_window = newwin(display->height+1,display->width,display->height,0);

  wmove(display->mode_window,0,0);
  struct buffer_region* cur = all_buffers->cur;
  wprintw(display->mode_window,
          "-[name:%s, pos:%d, start:xx, cursor:%d num_lines:%d ][%d x %d]",
          cur->buf_name,
          cur->cur_pos,
          display->cursor_line,
          cur->num_lines,
          display->height,
          display->width);

  int i  = 0;
  for(i  = 0; i < 50; i++)
    wprintw(display->mode_window,"%c", '-');
  wrefresh(display->mode_window);
}

void display_loop() {
  struct buffer_display* display = malloc(sizeof(struct buffer_display));
  display->height = 32;
  display->width = 1024;

  display->cursor_column = 0;
  display->cursor_line = 0;
  display->mode_window = NULL;
  display->buffer_window = NULL;
  display->start_line_ptr = all_buffers->cur->lines;
  display->current_line_ptr = display->start_line_ptr;

  char cur ;
  initscr();
  raw();
  refresh();
  bool redisplay = false;
  bool quit = false;

  while(!quit) {
    noecho();
    buffer_show(display);

    wrefresh(display->mode_window);
    wrefresh(display->buffer_window);
    redisplay = false;

    move(display->cursor_line,display->cursor_column);

    while(!redisplay) {
      mode_line_show(display);
      cur = getch();
      if (cur == 3 || cur == 'q') { // quit
        quit = true;
        break;
      } else if (cur == '<') {
        display->cursor_line = 0;
        redisplay = true;
        /*
        if(display->start_line-display->height < 0 ) {
          display->start_line = 0;
          continue;
        }
        //display->start_line -= display->height;
        */
        display_pg_up(display);
      } else if (cur == '>'){
        display->cursor_line = 0;
        redisplay = true;
        display_pg_down(display);
      } else if (cur == 'j') {
        if(display->cursor_line+1 >= display->height)  {
          display->cursor_line =0;
          redisplay = true;
          display_pg_down(display);
        } else {
          display_line_down(display);
          move(++display->cursor_line,display->cursor_column);
        }
      } else if (cur == 'k') {
        if(display->cursor_line -1 <= 0){
          display_pg_up(display);
          display->cursor_line = display->height;
          redisplay = true;
        } else {
                display_line_up(display);
                move(--(display->cursor_line),display->cursor_column);
        }
      } else if (cur == 'l') {
         if(display->cursor_column < display->width &&
            display->cursor_column < display->current_line_ptr->data_len) {
           move(display->cursor_line,(display->cursor_column)++);
         } else {
           display->cursor_column = display->current_line_ptr->data_len;
           move(display->cursor_line,display->cursor_column);
         }
      } else if (cur == 'h') {
        if(display->cursor_column > 0) {
          move(display->cursor_line,--(display->cursor_column));
        } else{
          display->cursor_column = 0;
          move(display->cursor_line,0);
        }
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

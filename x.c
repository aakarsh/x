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

struct buffer_region {

  int size;
  int end_pos;
  int cur_pos;
  
  int num_lines;
  
  /* If buffer is backed by a file */
  char* filepath;
  
  /**
   * Indices of the part of the file which this region represents.
   */
  long freg_begin;
  long freg_end;

  off_t fsize;

  char* buf_name;
  char* contents;
  
  struct buffer_region* prev;
  struct buffer_region* next;
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

struct buffer_region* buffer_alloc(int size,char* buffer_name) {

  struct buffer_region* buffer = malloc(sizeof(struct buffer_region));

  buffer->size = size;
  buffer->end_pos = 0;
  buffer->cur_pos  = 0;
  buffer->contents = malloc(size*sizeof(char));

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

inline void buffer_goto_line(struct buffer_region* buffer, int line_number){
  if(line_number > buffer->num_lines)
    line_number = buffer->num_lines;
  
  int i = 0;
  int cur_line = 0;
  int line_start_pos = 0;
  
  for( i = 0 ; i < buffer->end_pos && cur_line < line_number; i++) {
    if(buffer->contents[i] == '\n') {
      cur_line++;
      line_start_pos = i;
    }
  }
  
  buffer_set_point(buffer,line_start_pos);
}
  

inline void buffer_truncate(struct buffer_region* buffer){
  buffer->end_pos = 0;
}

inline int buffer_size(struct buffer_region* buffer) {
  return buffer->end_pos;
}

inline int buffer_remaining(struct buffer_region* buffer){
  return buffer->size - buffer->end_pos;
}


inline bool buffer_endp(struct buffer_region* buf) {
  return buf->cur_pos >=  buf->end_pos;
}

inline bool buffer_file_endp(struct buffer_region* buf) {
  return buf->freg_end >= (long) buf->fsize;
}

void buffer_expand(struct buffer_region* buffer, int new_size){
  if(new_size < buffer->size)
    return;

  char* new_contents = malloc(sizeof(char));

  // copy over new buffer contents
  int i = 0;
  for(i = 0; i < buffer->size; i++)
    new_contents[i] = buffer->contents[i];

  // free old contents
  free(buffer->contents);
  // exchange
  buffer->contents = new_contents;
}


/**
 * Appends the string to buffer NULL terminators are left out of the
 * buffer and added later. If NULL terminator exists in the string it
 * will prevent futher copying into the buffer.
 */
void buffer_append(struct buffer_region* buffer, const char* s, int size) {
  int i = 0;

  if(buffer_remaining(buffer) < size) {
    // Expand the buffer to accomodate new size.
    buffer_expand(buffer, buffer->end_pos + size);
  }

  while(buffer->end_pos < buffer->size && i < size) {
    char cur =  s[i++];
    if (cur == '\0')
      break;
    buffer->contents[buffer->end_pos++] = cur;
  }
}

/**
 * Reads num_bytes from the buffer into read_buffer, adding string
 * NULL terminator when its done. Passed in number of bytes don't
 * include the NULL terminator which will be appended to the line.
 */
int buffer_read(struct buffer_region* buffer, char* read_buffer, int num_bytes) {
  int read_bytes = 0;
  while(buffer->cur_pos < buffer->end_pos && read_bytes < num_bytes) {
    read_buffer[read_bytes++] = buffer->contents[buffer->cur_pos++];
  }
  read_buffer[read_bytes] = '\0';
  return read_bytes;
}

/**
 * Counts the number of lines in the buffer. This will involve going
 * through the contents of buffer checking for a new-line.
 */
int buffer_num_lines(struct buffer_region* buffer) {
  int number = 1;
  int i = 0;

  for(i = 0 ; i< buffer->end_pos; i++)
    if(buffer->contents[i] == '\n')
      number++;
  return number;
}

/**
 * Reads a line from the buffer but if there is no line terminor will
 * end up reading the whole buffer.
 */
int buffer_readline(struct buffer_region* buffer,char* read_buffer, int num_bytes) {
  int read_bytes = 0;

  if(buffer->cur_pos >= buffer->end_pos) {
    //    buffer_fill(buffer,buffer->
  }
  
  while(buffer->cur_pos < buffer->end_pos && read_bytes < num_bytes ) {
    char cur = buffer->contents[buffer->cur_pos++];
    if(cur  == '\n')
      break;
    read_buffer[read_bytes++] = cur;
  }
  read_buffer[read_bytes] = '\0';
  return read_bytes;
}

/**
 * Fill the rest of the buffer with the contents of the file. Stopping
 * when we reach the end of the file or the buffer.
 */
int buffer_fill(struct buffer_region* buffer, const char* file_name, long offset) {
  FILE* file =  fopen(file_name,"r");

  if(file == NULL) {
    return errno;
  }
  
  fseek(file,offset,0);
  int remaining = buffer_remaining(buffer);
  size_t read_bytes = fread(&(buffer->contents[buffer->end_pos]),1,remaining,file);

  buffer->end_pos   += (int) read_bytes;
  buffer->freg_begin = (long) offset;
  buffer->freg_end = (long) offset+read_bytes;
  buffer->num_lines  = buffer_num_lines(buffer);

  fclose(file);
  return 0;
}

/**
 * Replace buffer contents with next page form the file.
 */
int buffer_scroll_down(struct buffer_region* buffer){
  if(buffer->freg_end >=  buffer->fsize)
    return -1;
  buffer_fill(buffer,buffer->filepath,buffer->freg_end);
  return 0;
}

/**
 * If possible replace the buffer with contents of previous page
 */
int buffer_scroll_up(struct buffer_region* buffer){
  long new_offset = buffer->freg_begin - buffer->size; 
  if(new_offset  <= 0)
    new_offset = 0;  
  buffer_fill(buffer, buffer->filepath,new_offset);
}


/**
 * Opens a file given by file path 
 */
struct buffer_region* buffer_open_file(char* buffer_name, char* file_path) {
  struct buffer_region* buf = buffer_alloc(DEFAULT_BUFFER_SIZE,buffer_name);
  struct stat file_stat;

  if(0 == stat(file_path, &file_stat)) {
    buf->fsize = file_stat.st_size;
    int len = strlen(file_path);
    buf->filepath= malloc((len+1)*sizeof(char));
    strncpy(buf->filepath,file_path,len);
    buf->filepath[len]='\0';    
    buffer_fill(buf,file_path,0);
    return buf;
  } else{
    // perror
  }
  return NULL;
}

void test_buffer() {

  printf("test_buffer : ");

  struct buffer_region* buffer = buffer_alloc(1000,"foo");
  const char* lines[] = {"hello world ",
                         "how are you ", " hope all is well "};

  int i = 0;
  int total_line_size = 0;

  for(i = 0; i < ARRAY_SIZE(lines); i++) {
    int len = strlen(lines[i]);
    buffer_append(buffer,lines[i],len);
    total_line_size+= len;
  }

  for(i = 0; i < ARRAY_SIZE(lines); i++){
    int len = strlen(lines[i]);
    char* read_buffer = (char*) malloc(len * sizeof(char));
    buffer_read(buffer,read_buffer,len);
    assert(0 == strcmp(read_buffer,lines[i]));
    free(read_buffer);
  }

  // return to begining
  buffer_rewind(buffer);

  char cur_line[1024];
  buffer_readline(buffer,cur_line,1024);

  printf("pass\n");
}

void test_buffer_file() {
  printf("test_buffer_file : ");
  struct buffer_region* buffer = buffer_alloc(4096,"foo");
  buffer_fill(buffer,"/proc/vmstat",0);
  int num_lines  = buffer_num_lines(buffer);
  printf("num_lines:%d\n",num_lines);

  int i = 0;
  for(i = 0; i < num_lines; i++) {
    char cur_line[1024];
    buffer_readline(buffer,cur_line,1024);
    printw("%s\n",cur_line);
  }
  printf("pass\n");
}

struct buffer_display{
  int height;
  int width;
  int start_pos;
  int start_line;
  int cursor_column;
  WINDOW* mode_window;
  WINDOW* buffer_window;
};

void buffer_show(struct buffer_display* display) {
  if(display->buffer_window == NULL) {
    display->buffer_window = newwin(display->height,display->width,0,0);
  }

  struct buffer_region* buf = all_buffers->cur;
  
  wmove(display->buffer_window,0,0);
  wrefresh(display->buffer_window);
  
  int i = 0;
  buffer_goto_line(buf,display->start_line);
  
  for(i = 0 ;  i < display->height; i++) {
    
    char cur_line[1024];
    buffer_readline(buf,cur_line,1024);
    wprintw(display->buffer_window,"%s\n",cur_line);
    
    // Reached end of buffer but file has more bytes
    if(buffer_endp(buf) && !buffer_file_endp(buf)) {
      buffer_scroll_down(buf);
    }
  }
}


/**
 * Show the mode-line at the bottom of the display.
 */
void mode_line_show(struct buffer_display* display) {
  if(display->mode_window == NULL)
    display->mode_window = newwin(display->height+10,display->width,display->height,0);

  wmove(display->mode_window,0,0);
  struct buffer_region* cur = all_buffers->cur;
  wprintw(display->mode_window,"-[%s, %d, %d]",cur->buf_name,cur->cur_pos,display->start_line);
  int i  = 0;
  for(i  = 0; i < 50; i++)
    wprintw(display->mode_window,"%c", '-');
  wrefresh(display->mode_window);
}

void display_loop() {
  
  struct buffer_display* display = malloc(sizeof(struct buffer_display));
  display->height = 32;
  display->width = 1024;
  display->start_pos =0;
  display->cursor_column = 0;
  display->mode_window = NULL;
  display->buffer_window = NULL;
  
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
    move(display->start_line,display->cursor_column);
    while(!redisplay) {
      mode_line_show(display);
      cur = getch();
      if (cur == 3 || cur == 'q') { // quit
        quit = true;
        break;
      } else if (cur == 'j') {
        move(++display->start_line,display->cursor_column);
        if(display->start_line > display->height){
          redisplay = true;
        }        
      } else if (cur == 'k') {
        move(--(display->start_line),display->cursor_column);
        if(display->start_line < 0){
          redisplay = true;
        }
      } else if (cur == 'l') {
        if(display->cursor_column < display->width)
          move(display->start_line,++(display->cursor_column));
      } else if (cur == 'h') {
        if(display->cursor_column > 0)
          move(display->start_line,--(display->cursor_column));
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

#include <ncurses.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>

#define ARRAY_SIZE(x) ((sizeof x) / (sizeof *x))
static  int DISPLAY_SIZE = 512;
static  int DEFAULT_BUFFER_SIZE = 4096;

struct buf {
  int size;
  int end_pos;
  int cur_pos;

  int buf_name_len;
  char* buf_name;

  char* contents;
};

struct buffer_list {
  int num_buffers;
  struct buf* cur;
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

struct buf* buffer_alloc(int size){
  struct buf* buffer = malloc(sizeof(struct buf));
  buffer->size = size;
  buffer->end_pos = 0;
  buffer->cur_pos  = 0;
  buffer->contents = malloc(size*sizeof(char));
}

struct buf* buffer_create() {
}

inline void buffer_rewind(struct buf* buffer){
  buffer->cur_pos = 0;
}

inline void buffer_set_point(struct buf* buffer , int point){
  if(point < buffer->end_pos)
    buffer->cur_pos = point;
}

inline void buffer_truncate(struct buf* buffer){
  buffer->end_pos = 0;  
}

inline int buffer_size(struct buf* buffer) {
  return buffer->end_pos;
}

inline int buffer_remaining(struct buf* buffer){
  return buffer->size - buffer->end_pos;
}

void buffer_expand(struct buf* buffer, int new_size){
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
void buffer_append(struct buf* buffer, const char* s, int size) {
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
int buffer_read(struct buf* buffer, char* read_buffer, int num_bytes) {  
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
int buffer_num_lines(struct buf* buffer) {  
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
int buffer_readline(struct buf* buffer,char* read_buffer, int num_bytes) {
  int read_bytes = 0;  
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
int buffer_fill(struct buf* buffer, const char* file_name) {  
  FILE* file =  fopen(file_name,"r");
  
  if(file == NULL) {
    return errno;
  }
  
  int remaining = buffer_remaining(buffer);
  size_t read_bytes = fread(&(buffer->contents[buffer->end_pos]),
                            1,remaining,file);
  buffer->end_pos+=(int)read_bytes;

  fclose(file);
  return 0;
}


void test_buffer() {

  printf("test_buffer : ");

  struct buf* buffer = buffer_alloc(1000);
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
  struct buf* buffer = buffer_alloc(4096);  
  buffer_fill(buffer,"/proc/vmstat");
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
};

void buffer_show(struct buffer_display* display) {
  struct buf* buf = all_buffers->cur;
  buffer_set_point(buf,display->start_pos);
  int nlines = buffer_num_lines(buf);
  move(0,0);
  int i = 0;
  for(i =0 ; i < nlines && i < display->height; i++){
    char cur_line[1024];
    buffer_readline(buf,cur_line,1024);
    if (i > display->start_line) {
      printw("%s\n",cur_line);
    }
  }
}

void display_loop() {
  struct buffer_display* display = malloc(sizeof(struct buffer_display));
  display->height = 32;
  display->width = 1024;
  display->start_pos =0;

  char cur ; 
  initscr();
  raw();
  
  while(1) {    
    noecho();
    
    buffer_show(display);
    refresh();
    cur = getch();
    if (cur == 3) // quit
        break;
    if (cur == 'j')
      display->start_line++;
    if (cur == 'k')
      display->start_line--;
  }
  endwin();
}


int main(int argc, char* argv[]){  
  all_buffers = buffer_list_create();
  all_buffers->cur = buffer_alloc(DEFAULT_BUFFER_SIZE);
  buffer_fill(all_buffers->cur,"/home/aakarsh/src/c/x/x.c");
  display_loop();
  return 0;
}

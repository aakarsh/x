#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>

#define ARRAY_SIZE(x) ((sizeof x) / (sizeof *x))
                                     
/**
 * Creates a string with the character duplicated n-times
 */
char* dup(char c , int n) {
  int i;
  char* s = (char*)malloc((n+1)*sizeof(char));
  for(i = 0 ; i < n ; i++)
    s[i] = c;
  return s;
}

void test_dup(){
  printf("test_dup : ");
  char* eq = dup('=',2);
  assert(0 == strcmp(eq,"=="));
  printf("pass\n");
}

void print_border(char c, int n){
  char* border = dup(c,n);
  printf("%s\n",border);
  free(border);
}

/**
 * Demonstration of swapping of two integers using pointers. Even
 * though the pointers are passed in by value the swap happesn on the
 * references.
 */
void kr_swap(int* px , int* py){
  int tmp = *px;
  *px =*py;
  *py = tmp;
}

void test_kr_swap(){
  printf("test_kr_swap : ");
  int x = 0;
  int y = 1;
  kr_swap(&x,&y);
  assert(x == 1);
  assert(y == 0);
  printf("pass\n");
}

/**
 * Parses a float from the string. If it is not parable just returns
 * 0.  k&r pg 97.
 */
float parse_float(const char* str , int size){  
  int i = 0;
  int num = 0;
  float dec = 0;
  int sign = 1;
  while(i < size && isspace(str[i++]));

  i--;

  if(i == size)
    return 0;

  if(!isdigit(str[i]) && (str[i] == '-' || str[i] == '+'))
    sign = (str[i++] == '-' ) ? -1 : 1;
  
  while(i < size && isdigit(str[i])) {
    num *= 10;
    num += (str[i++]-'0');
  }
  
  if(str[i++] != '.')
    return (float) sign*num;

  int dec_start = i;
  
  while(i < size && isdigit(str[i]))
    dec = dec +  (pow(10, dec_start - i -1) * ( str[i++]-'0'));

  return sign*(num+dec);
  
}


void test_parse_float() {
  int i;
  printf("test_parse_float : ");

  const char*  tests[] ={"3.14",".14","-.14","+.14","0","+0.00","-0.00","-3.14%","junk1"};
  float nums[]        = { 3.14,.14,-.14,.14,0,0,0,-3.14,0};
  
  for(i = 0; i < ARRAY_SIZE(nums); i++) {
    float got = parse_float(tests[i],strlen(tests[i]));
    assert(0.0 == (nums[i]-got));
  }
  
  printf("pass\n");
}


struct buf {
  int size;
  int end_pos;
  int cur_pos;
  char* contents;
};

struct buf* buffer_alloc(int size){
  struct buf* buffer = malloc(sizeof(struct buf));
  buffer->size = size;
  buffer->end_pos = 0;
  buffer->cur_pos  = 0;
  buffer->contents = malloc(size*sizeof(char));
}

void buffer_rewind(struct buf* buffer){
  buffer->cur_pos = 0;
}

void buffer_truncate(struct buf* buffer){
  buffer->end_pos = 0;  
}

int buffer_size(struct buf* buffer) {
  return buffer->end_pos;
}

/**
 * Appends the string to buffer NULL terminators are left out of the
 * buffer and added later. If NULL terminator exists in the string it
 * will prevent futher copying into the buffer.
 */
void buffer_append(struct buf* buffer, const char* s, int size) {
  int i = 0;
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

void test_buffer() {

  printf("test_buffer : ");

  struct buf* buffer = buffer_alloc(1000);
  const char* lines[] = {"hello world ","how are you ", " hope all is well "};
  
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


int main(int argc, char* argv[]){
  print_border('-',70);
  test_kr_swap();
  test_dup();
  test_parse_float();
  test_buffer();
  print_border('-',70);  
  return 0;
}

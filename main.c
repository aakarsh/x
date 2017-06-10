#include <stdio.h>
#include <assert.h>

void print_border(char c, int n){
  int i;
  char border[n+1];
  for(i = 0 ; i < n ;i++)
    border[i] = c;
  border[n] ='\0';
  printf("%s\n",border);    
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

int main(int argc, char* argv[]){
  print_border('-',70);
  test_kr_swap();
  print_border('-',70);
  return 0;
}

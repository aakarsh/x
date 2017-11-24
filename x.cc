#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>

#include <memory>

using namespace std;

class AppProperties {
  friend class Logger;
  
public:
  static bool debugMode;
  static string debugLogFile;
};

#ifdef DEBUG
bool AppProperties::debugMode = true;
#else
bool AppProperties::debugMode = false;
#endif

string AppProperties::debugLogFile = "x-debug.log";

const char* log_file ="x.log";

enum log_level { LOG_LEVEL_INFO, LOG_LEVEL_DEBUG};

class Logger {
private:
public:
  log_level level;
  FILE* debug_file;
  ifstream debug_stream;
  static bool debug_mode;

  Logger():
      level(LOG_LEVEL_INFO)
      , debug_stream(AppProperties::debugLogFile, std::ifstream::in)
  {
    if(AppProperties::debugMode) {
      this->level = LOG_LEVEL_DEBUG;
      this->debug_file = fopen(AppProperties::debugLogFile.c_str(),"w+");
      this->debug_stream.open(AppProperties::debugLogFile, std::ifstream::in);
    }
  }

  ~Logger() {
#ifdef DEBUG
    // this->debug_file.close();
    // fclose(this->debug_file);
#endif
  }
};


class Line {
public:
  // Position relative to the file.
  long line_number;
  streampos file_position;
  long line_pos;

  // Line Data
  string data;

  Line(int line_no, streampos  fpos, int lpos):
    line_number(line_no)
    ,file_position(fpos)
    ,line_pos(lpos) {}

  Line(int line_no, streampos  fpos, int lpos,string& data):
    line_number(line_no)
    ,file_position(fpos)
    ,line_pos(lpos)
    ,data(data){}

  Line(): Line(0,0,0) {}
};

class Buffer {

private:
  // List of buffer errors
  enum BufferError { Buffer_NoError,
                     Buffer_FileError,
                     Buffer_NoFile } ;

  // file backing this buffer
  const string filePath;

  // buffer name
  const string bufferName;

  // file stream backing the buffer.
  fstream bufferStream;

  BufferError errorCode;

  // size of buffer.
  off_t size;

  off_t fsize;
  bool modified;

  // current line
  int currentLineIndex ;

  // list of lines of the buffer.
  vector<Line*> lines;

public:

  vector<Line*>& getLines() {
    return this->lines;
  }
  
  bool isModified() {
    return this->modified;
  }
  
  string getBufferName() {
    return this->bufferName;
  }

  // Avoid defaults
  Buffer()  = delete;

 Buffer(const Buffer& buffer)  = delete;

  Buffer(string name, string path):
      filePath(path)
    , bufferName(name)
    , bufferStream(path, ios_base::in)  {

    if(bufferStream.rdstate() && std::ifstream::failbit != 0) {
      errorCode = Buffer_NoError;
      return;
    }

    this->fill(bufferStream);
  }

  ~Buffer() {
    // close open file
    bufferStream.close();
    // free all lines.
    this->clear();
  }

  bool isErrorState() {
    return errorCode != Buffer_NoError;
  }

  /**
   * Drop all saved lines.
   */
  void clear() {
    for(auto it = lines.begin() ; it != lines.end() ; it++) {
      delete *it;
    }
    lines.clear();
  }

  /**
   * Fill buffer with lines from the input stream.
   */
  void fill(istream& in) {

    string line;
    int line_number = 0;

    this->clear();

    while(getline(in,line)) {
      Line* cur =  new Line(line_number,in.tellg(),0,line);
      lines.push_back(cur);
    }
  }

};


class BufferList {

private:
  vector<Buffer*> buffers;
  Buffer* currentBuffer;

public:
  BufferList() = default;

  BufferList(Buffer* first):
    currentBuffer(first) {
    this->buffers.push_back(first);
  }

  BufferList& append(Buffer* buffer) {
    return this->append(*buffer);
  }

  BufferList& append(Buffer& buffer) {
    this->buffers.push_back(&buffer);
    this->currentBuffer = &buffer;
    return *this;
  }

  BufferList &operator+=(Buffer* buffer) {
    return this->append(*buffer);
  }

  BufferList &operator+=(Buffer& buffer) {
    return this->append(buffer);
  }

  int numBuffers()  {
    return this->buffers.size();
  }

  Buffer* getCurrentBuffer() {
    return this->currentBuffer;
  }

};



class DisplayWindow {
  
private:
  int numLines;
  int numColumns;
  int beginY;
  int beginX;

  WINDOW* window;

public:
  // using managed resource window
  DisplayWindow () = delete;
  DisplayWindow& operator=(const DisplayWindow& ) = delete;

  DisplayWindow(int nl, int nc,int by, int bx):
     numLines(nl)
    ,numColumns(nc)
    ,beginY(by)
    ,beginX(bx) {

    window = newwin(numLines,
                    numColumns,
                    beginY,
                    beginX);
  }

  int getNumLines() { return numLines; };

  DisplayWindow& refresh() {
    wrefresh(window);
    return *this;
  }

  DisplayWindow& moveCursor(int y, int x){
    wmove(window,y,x);
    return *this;
  }

  DisplayWindow& displayLine(int y, int x, string line) {
    wmove(window,y,x);
    wprintw(window,line.c_str());
    wrefresh(window);
    return *this;
  }
  
  DisplayWindow& displayLine(string line) {
    wprintw(window,line.c_str());
    wrefresh(window);
    return *this;
  }

  ~DisplayWindow() {
    delwin(window);
  }

};


class Display;
class DisplayCommand;
class Mode;
enum  DisplayMode { CommandMode = 0,
                    InsertMode  = 1,
                    SearchMode  = 2 };

class DisplayCommand {
  friend class Display;
public:
  virtual DisplayMode run(Display &display) = 0;
};

class DisplayNextLine : public DisplayCommand {  
public:  
  DisplayMode run(Display& display) {    
    return CommandMode;
  }  
};

typedef map<string,DisplayCommand*> keymap;

class Mode {
private:
  string modeName;
  keymap modeMap;
  
public:
  Mode(const string& name, const keymap &cmds) :    
     modeName(name)
    ,modeMap(cmds){}
    
  DisplayCommand* lookup(const string& cmd) {
    return modeMap[cmd];
  }
};


class Display {

private:



  vector<Mode*> modes;

  int height;
  int width;
  int curserLine;
  int cursorColumn;

  DisplayMode mode;

  DisplayWindow *modeWindow;
  DisplayWindow *bufferWindow;

  BufferList *buffers;

  bool redisplay; // trigger a buffer-redisplay of buffer
  bool quit;      // quit will cause the display loop to exit.
  //CommandKeyMap keymap
public:

  Display() : buffers(new BufferList()) {

    // determine the screen
    initscr();

    // initialized the height and width
    getmaxyx(stdscr, this->height, this->width);

    this->modeWindow    = new DisplayWindow(height-1,width,height-2,0);
    this->bufferWindow  = new DisplayWindow(height-2,width,0,0);

    keymap cmdMap;
    cmdMap["j"] =  new DisplayNextLine();
    string cmdModeName("CMD");
    
    this->modes.push_back(new Mode("CMD", cmdMap));
    this->mode = CommandMode;    
    this->height -= 2; // leave space

    raw();
    refresh();
  }
  
  Mode* getCurrentMode() {
    return this->modes[mode];
  }

  void changeMode(DisplayMode newMode) {
    if(newMode!= mode){
      mode = newMode;
    }
  }
  
  void runCommand(const string& cmd) {
    if(cmd == "q") { // treat quit special for nwo
      this->quit = true;      
    }else { // Need to look up command in the mode
      this->redisplay = false; // Don't do redisplay unless requested
      
      Mode* mode = this->getCurrentMode();
      
      if (!mode)
        return;
      
      DisplayCommand* displayCommand = mode->lookup(cmd);
      if(!displayCommand)
        return;

      DisplayMode nextMode = displayCommand->run(*this);
      this->changeMode(nextMode);
      
    }    
  }

  void displayModeLine() {
    
    Buffer* currentBuffer =
      this->buffers->getCurrentBuffer();
    
    string modified =
      currentBuffer->isModified() ? "*" : "-";
    
    stringstream modeLine;
    modeLine<<"["<<modified<<"] "<< currentBuffer->getBufferName();

    this->modeWindow->displayLine(0,0,modeLine.str());
  }

  void displayBuffer(bool redisplay) {

    this->bufferWindow->moveCursor(this->curserLine,this->cursorColumn);
    
    Buffer* buffer =
      this->buffers->getCurrentBuffer();
    
    vector<Line*> lines = buffer->getLines();
    int lineCount;
    for(auto it = lines.begin() ; it != lines.end(); it++){
      if(lineCount >= this->bufferWindow->getNumLines())
        break;
      this->bufferWindow->displayLine((*it)->data);
      this->bufferWindow->displayLine("\n");
      lineCount++;
    }
  }

  void start() {
    this->quit = false;
    
    while(!this->quit) { // quit      
      noecho();

      // main buffer 
      this->displayBuffer(true);
      
      // mode line 
      this->displayModeLine();

      // trigger command
      string cmd(1,getch());
      this->runCommand(cmd);
      
      // run the next command till redisplay becomes necessary
      while(!this->redisplay && !this->quit) {
        this->runCommand(string(1,getch()));   // get-input
      }
      
      // need to reset to do a redisplay
      this->redisplay = false;
    }
    
    return;
  }


  /**
   * Display will handle the life cycle of th
   * buffer once it has been added to display's
   * buffer list.
   */
  Display &operator+=(Buffer* buffer) {
    this->buffers->append(buffer);
    return *this;
  }

  ~Display(){    
    endwin();    
    // display manages buffers and its windows.
    delete buffers;
    delete modeWindow;
    delete bufferWindow;
  }

};
  

#define ARRAY_SIZE(x) ((sizeof x) / (sizeof *x))
char* strlinecat(char * l0,char* l1);
char* xstrcpy(char* str);

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

  /// Buffer searching
  bool found;
  int line_number;
  int line_column;

  struct line* found_line;
};

 //
 // Used to represent a particular buffer.
 //
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

 //
 // Used to represent a list of all open buffers.
 //
struct buffer_list {
  struct buffer_list* next;
  int num_buffers;
  struct buffer* cur;
};

enum display_mode
  { INSERT_MODE,
    COMMAND_MODE,
    SEARCH_MODE
  };

struct display {
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


Logger* XLOG;

#define LOG_DEBUG( ...)                         \
  do {                                          \
    if((XLOG)->level >=  LOG_LEVEL_DEBUG) {     \
      fprintf((XLOG)->debug_file,__VA_ARGS__);  \
      fflush((XLOG)->debug_file);               \
    }                                           \
  } while(0);


static
void
line_insert(struct line* new_line,
            struct line* prev_line,
            struct line** line_head);
static
struct line*
line_merge(struct line* line,
           struct line** list_head);
static
struct line*
line_split(struct line* line,
           int split_pos,
           struct line** line_head);

static
struct line*
line_unlink(struct line* line,
            struct line** line_head);




static
struct buffer_list*
buffer_list_create();

static
struct buffer*
buffer_create(char* buffer_name);


//static
//struct line*
//buffer_find_line(struct buffer* b,
//                 int num);


static
int
buffer_fill_lines(struct buffer* buffer,
                  FILE* file,
                  long offset);

static
void
buffer_save(struct buffer* buffer);

static
struct buffer*
buffer_open_file(char* buffer_name,
                 char* path);

static
void
buffer_insert_char(struct buffer* buffer,
                   char insert_char,
                   int insert_position);

static
void
buffer_open_line(struct buffer* buffer);

static
char *
buffer_delete_current_line(struct buffer* buffer);

static
bool
buffer_split_line(struct buffer* buffer,
                  int split_postion);

static
void
buffer_join_line(struct buffer* buffer);

static
void
buffer_delete_char(struct buffer* buffer,
                   int delete_position);

static
struct line*
buffer_search_forward(struct buffer* buffer,
                      int start_line);

static
void
buffer_search_free(struct buffer* buffer);

static
void
buffer_search_alloc(struct buffer* buffer,
                    char* search_term,
                    int start_line);
static
void
display_set_buffer(struct display* display,
                   struct buffer* buffer);
static
void
display_line_up(struct display * display);
static
bool
display_line_down(struct display * display);
static
bool
display_pg_down(struct display * display);
static
bool
display_pg_last(struct display* display,
                void* misc);
static
bool
display_pg_up(struct display * display);
static
bool
display_pg_up_begin(struct display * display,
                    void* misc);
bool
display_pg_down_begin(struct display * display,
                      void* misc);

/// static
/// bool
/// display_pg_up_end(struct display * display,
///                   void* misc);
static
bool
display_end_of_line(struct display* display,
                    void* misc);
static
bool
display_begining_of_line(struct display* display,
                         void* misc);
static
bool
display_to_insert_mode(struct display* display,
                       void* misc);
static
bool
display_to_command_mode(struct display* display,
                        void* misc);

static
bool
display_empty_linep(struct display* display);

static
bool
display_cursor_bolp(struct display* display);

static
bool
display_on_first_linep(struct display* display);

static
bool
display_on_last_linep(struct display* display);

static
bool
display_move_line_up(struct display* display,
                     void* misc);

static
bool
display_move_line_down(struct display* display,
                       void* misc);
static
bool
display_move_left(struct display* display,
                  void* misc);

static
bool
display_move_right(struct display* display,
                   void* misc);

static
bool
display_join(struct display* display,
             void* misc);
static
bool
display_delete_char(struct display* display,
                    void* misc);

static
bool
display_delete_line(struct display* display,
                    void* misc);

static
bool
display_quit(struct display* display,
             void* misc);
static
bool
display_save(struct display* display,
             void* misc);

static
void
display_redraw(struct display* display);

static
int
display_line_number(struct display* display);

// static
// bool
// display_cursor_within_line(struct display* display);


static
bool
display_insert_cr(struct display* display,
                  void* misc);

static
bool
display_open_line(struct display* display,
                  void* misc);
static
bool
display_startlinep(struct display* display);

static
bool
display_insert_char(struct display* display,
                    void* misc);

static
bool
display_bleep(struct display* display,
              void* misc);

static
bool
display_goto_position(struct display* display,
                      int nline,
                      int column);

static
bool
display_insert_tab(struct display* display,
                   void* misc);
static
bool
display_insert_backspace(struct display* display,
                         void* misc);

static
bool
display_search(struct display* display,
               char* search,
               int current_line);

static
bool
display_search_next(struct display* display,
                    void* unused);
static
bool
display_search_prev(struct display* display,
                    void* misc);

static
bool
display_start_search(struct display* display,
                     void* misc);

static
bool
display_cmd_mode(struct display* display,
                 void* misc);

static
struct display*
display_init(struct buffer* buffer,
             int height,
             int width);

static
bool
display_run_cmd(struct display* display,
                void* misc);


/**
 * Creates a line initialized with the copy of data;
 */
struct line*
line_create(char* data)
{
  struct line* newline =
    (struct line*) malloc(sizeof(struct line));
   newline->line_number = -1;
   newline->file_position = -1;
   newline->data = xstrcpy(data);
   newline->data_len = strlen(newline->data);
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
  //
  if(prev_line != NULL) {
    LOG_DEBUG("Try int insert [%s] after [%s]\n",new_line->data,prev_line->data);
    new_line->prev = prev_line;
    struct line* old_next = prev_line->next;
    prev_line->next = new_line;
    new_line->next = old_next;
    //
    if(NULL !=old_next) {
      old_next->prev = new_line;
    }
    //
  } else{ // first line
    *line_head = new_line;
  }
}

/**
 * *TODO* Merge the current line with the previous line. Due to the
 * presence of new line. When this line is merged with previous line
 * we will need to handle the newline.
 */
struct line*
line_merge(struct line* line,
           struct line** list_head)
{
  if(NULL == line->prev )
    return line;

  struct line* line_prev = line->prev;

  char* joined_lines = strlinecat(line_prev->data, line->data);

  if(NULL == joined_lines )
    return line;

  line_prev->data_len = strlen(joined_lines);

  line_prev->next = line->next;

  return line_prev;
}

struct line*
line_split(struct line* line,
           int split_pos,
           struct line** line_head)
{
  char* line_data = line->data;
  int line_len = strlen(line_data);

  if(split_pos >= line_len)
    return line;

  struct line* new_line = (struct line*) malloc(sizeof(struct line));

  int l2 = (line_len - split_pos)+1;

  new_line->data  = (char*) malloc(l2 * sizeof(char));
  strncpy(new_line->data,line->data+split_pos,l2);

  LOG_DEBUG( "line_split: new_line : %s" ,new_line->data);
  new_line->data_len = strlen(new_line->data);


  line->data = (char*) realloc(line->data,split_pos+2);
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
line_unlink(struct line* line,
            struct line** line_head)
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

/**
 * Buffered List
 */
static struct buffer_list* buffers;


struct buffer_list*
buffer_list_create()
{
  struct buffer_list* list =  (struct buffer_list*) malloc(sizeof(struct buffer_list));
  list->num_buffers = 0;
  list->cur = NULL;
  list->next = NULL;
  return list;
}

struct buffer*
buffer_create(char* buffer_name)
{
  struct buffer* buffer = (struct buffer*) malloc(sizeof(struct buffer));
  buffer->search = NULL;
  buffer->buf_name = xstrcpy(buffer_name);
  return buffer;
}

/**
 * Return a pointer to a line rather than using indices into content
 * field.
 */

struct line*
buffer_find_line(struct buffer* b,
                 int num)
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
                  FILE* file,
                  long offset)

{
  fseek(file,offset,0);

  ssize_t read;
  size_t len = 0;
  char* line = NULL;

  long line_number = 0;
  long file_position = 0;

  buffer->lines  = NULL;
  buffer->modified = false;

  struct line* prev_line = NULL;

  while((read = getline(&line,&len,file)) != -1) {

    struct line* cur_line = (struct line*)malloc(sizeof(struct line));

    cur_line->prev = NULL;
    cur_line->next = NULL;

    if(prev_line != NULL) {
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
buffer_open_file(char* buffer_name,
                 char* path)
{
  struct stat file_stat;

  if(0 == stat(path, &file_stat)) {

    struct buffer* buf = buffer_create(buffer_name);
    buf->fsize = file_stat.st_size;
    buf->size = (long) buf->fsize; //unused
    buf->filepath = xstrcpy(path);

    FILE* file =  fopen(buf->filepath,"r");

    if(file == NULL) {
      return NULL;
    }
    buffer_fill_lines(buf,file,0);
    return buf;
  } else {
    // file does not exist
    struct buffer* buf = buffer_create(buffer_name);
    buf->fsize = 0;
    buf->filepath = xstrcpy(path);
    buf->num_lines = 0;
    buf->modified = false;
    buf->lines = line_create("");
    buf->current_line = buf->lines;
    return buf;
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

  char * modified_line = (char*) realloc(buffer->current_line->data,(size_t) (len_line+2));

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

   struct line* newline = (struct line*) line_create("\n");
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
  char * modified_line = (char*) realloc(buffer->current_line->data,(size_t) len_line);

  if(modified_line == NULL) // data is left unmodified by realloc
    return;

  buffer->modified = true;
  buffer->current_line->data = modified_line;
  buffer->current_line->data_len = strlen(buffer->current_line->data);
}

/**
 * A primitive search forward in the buffer.
 */
struct line*
buffer_search_forward(struct buffer* buffer,
                      int start_line)
{
  char* search = buffer->search->str;
  int line_num = buffer->search->line_number;

  struct line* line = NULL;
  char* found  = NULL;

  for(line = buffer->lines; line != NULL; line = line->next,line_num++) {

    if(line_num < start_line && line_num < buffer->search->line_number)
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
 * Go to the last page.
 */
bool
display_pg_last(struct display* display,void* misc)
{
  long pg_size = display->height;
  int cur_line = 0;

  // assume we start at current current page.
  struct line* pg_start = display->start_line_ptr;
  // to keep pages aligned
  struct line* cur = display->start_line_ptr;
  struct line* cur_prev = cur;
  while( NULL != cur ) {
    if(cur_line > pg_size) {
      pg_start = cur;
      cur_line = 0;
    }
    cur_prev = cur;
    cur = cur->next;
    cur_line++;
  }

  display->start_line_ptr = pg_start;
  display->current_buffer->current_line = cur_prev;
  display->cursor_line = cur_line - 1;

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

bool
display_pg_up_begin(struct display * display,void* misc)
{
  display_pg_up(display);
  display->cursor_line = 0;
  return true;
}

bool
display_pg_down_begin(struct display * display,void* misc)
{
  display_pg_down(display);
  display->cursor_line = 0;
  return true;
}

/**
bool
display_pg_up_end(struct display * display,void* misc)
{
  display_pg_up(display);
  display->cursor_line = display->height;
  return true;
}
*/
/**
 * return - Value will indicate whether full redisplay is required.
 */
bool
display_end_of_line(struct display* display,
                    void* misc)
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
display_begining_of_line(struct display* display,
                         void* misc)
{
  display->cursor_column = 0;
  wmove(display->buffer_window,display->cursor_line,display->cursor_column);
  wrefresh(display->buffer_window);
  move(display->cursor_line,display->cursor_column);
  return false;
}


bool
display_to_insert_mode(struct display* display,void* misc)
{
  display->mode = INSERT_MODE;
  move(display->cursor_line,display->cursor_column);
  return false;
}

bool
display_to_command_mode(struct display* display,void* misc)
{
  display->mode = COMMAND_MODE;
  move(display->cursor_line,display->cursor_column);
  return true;
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

bool
display_move_line_up(struct display* display, void* misc)
{
  bool redisplay = false;
  if(display_on_first_linep(display)) {
    move(display->cursor_line,display->cursor_column);
    return redisplay;
  }
  if(display->cursor_line - 1 < 0){
    display_pg_up(display);
    display->cursor_line = display->height;
    redisplay = true;
  } else {
    display_line_up(display);
    move(--(display->cursor_line),display->cursor_column);
  }
  return redisplay;
}

bool
display_move_line_down(struct display* display,
                       void* misc)
{
  bool redisplay = false;
  if(display_on_last_linep(display)) {
    move(display->cursor_line,display->cursor_column);
    return redisplay;
  }
  if(display->cursor_line + 1 >= display->height)  {
    display->cursor_line =0;
    redisplay = true;
    display_pg_down(display);
  } else {
    display_line_down(display);
  }
  return redisplay;
}

bool
display_move_left(struct display* display,
                       void* misc)
{
  bool redisplay = false;
  if(display->cursor_column < display->width &&
     display->cursor_column < display->current_buffer->current_line->data_len -1) {
    move(display->cursor_line,display->cursor_column++);
  } else {
    display->cursor_column = display->current_buffer->current_line->data_len;
    move(display->cursor_line,display->cursor_column);
  }
  return redisplay;
}

bool
display_move_right(struct display* display,
                   void* misc)
{
  if(display->cursor_column > 0) {
    move(display->cursor_line,--(display->cursor_column));
  } else{
    display->cursor_column = 0;
    move(display->cursor_line,0);
  }
  return false;
}

bool
display_join(struct display* display,void* misc)
{
  buffer_join_line(display->current_buffer);
  display->cursor_line -= 1;
  move(display->cursor_line,display->cursor_column);
  return true;
}

bool
display_delete_char(struct display* display,void* misc)
{
  buffer_delete_char(display->current_buffer,
                     display->cursor_column);
  return true;
}

bool
display_delete_line(struct display* display,
                    void* misc)
{
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
  return true;
}

/**
 * Close ncurses session and quit.
 */
bool
display_quit(struct display* display,
             void* misc)
{
  endwin();
  exit(1);
}

bool
display_save(struct display* display,
             void* misc)
{
  buffer_save(display->current_buffer);
  move(display->cursor_line,display->cursor_column);
  return false;
}

void
display_redraw(struct display* display)
{
  LOG_DEBUG("Called: display_redraw \n");

  wmove(display->buffer_window,0,0);
  wrefresh(display->buffer_window);

  int i = 0;
  int cnt = 0;
  struct line* start = display->start_line_ptr;
  struct line* prev = NULL;

  while(start!=NULL && i < display->height) {
    wprintw(display->buffer_window,"%s",start->data);
    prev = start;
    start = start->next;
    i++;
    cnt++;
  }
  bool no_ending_newline = false;
  if(prev)
    no_ending_newline = (NULL == strchr(prev->data,'\n'));
  // fill rest of screen with blanks
  for(; i < display->height; i++) {

    if(no_ending_newline && i == cnt)
      wprintw(display->buffer_window,"\n");

    wprintw(display->buffer_window,"~\n");
  }
}

/**
 * Recomputes the display line number may not be as efficient as
 * keeping display line number in-sync with buffer line number.
 */
int
display_line_number(struct display* display)
{
  struct line* line  = display->current_buffer->current_line;
  struct line* l  = display->start_line_ptr;
  int nline  = 0;
  while(l != line)
    nline++;
  return nline;
}

/**
 * Test that we are at the end of the current buffer's  line;
 */
/*
bool
display_cursor_eolp(struct display* display)
{
  int last_postion = display->current_buffer->current_line->data_len;
  return display->cursor_column == last_postion;
}
*/

 /**
bool
display_cursor_within_line(struct display* display)
{
  int last_postion = display->current_buffer->current_line->data_len;
  return display->cursor_column >=0  && display->cursor_column <= last_postion;
}
 */

/**
 * Handle carriage return in insert mode.
 */
bool
display_insert_cr(struct display* display,void* misc)
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
display_open_line(struct display* display,void* misc)
{
  LOG_DEBUG("called display_insert_cr line[%d] col:[%d] \n",
            display->cursor_line,display->cursor_column);
  buffer_open_line(display->current_buffer);

  display->cursor_line+=1;
  move(display->cursor_line,display->cursor_column);
  display->mode = INSERT_MODE;
  return true;
}

bool
display_startlinep(struct display* display)
{
  return display->start_line_ptr == display->current_buffer->current_line;
}

bool
display_insert_char(struct display* display,
                    void* misc)
{
  char* cur = (char*) misc;
  buffer_insert_char(display->current_buffer, *cur, display->cursor_column);
  display->cursor_column += 1;
  move(display->cursor_line,display->cursor_column);
  return true;
}

bool
display_bleep(struct display* display,
              void* misc)
{
  beep();
  flash();
  return true;
}

/**
 * Moves cursor to a particular line or column in the buffer. If the
 * line lies out of current sceen. Scrolls the buffer so that the
 * matching line shows on the last line of column
 */
bool
display_goto_position(struct display* display,
                      int nline,
                      int column)
{
  struct line* line = display->current_buffer->lines;
  struct line* start = line;

  int n = 0;
  int pos = 0;

  while (n < nline && NULL != line ) {
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

bool
display_insert_tab(struct display* display,void* misc)
{
  int tab_width = 8;
  char c = ' ';
  int i  = 0;
  for(i = 0; i< tab_width;i++)
    display_insert_char(display,&c);
  return true;
}
/**
 * The cases are
 */
bool
display_insert_backspace(struct display* display,void* misc)
{
  bool redisplay = true;
  LOG_DEBUG("backspace \n");

  if(!display_cursor_bolp(display)) { // if not begining of the line
    display->cursor_column -= 1;
    buffer_delete_char(display->current_buffer, display->cursor_column);
    move(display->cursor_line,display->cursor_column);
  } else {     // We are at the begining of the line

    if(!display_startlinep(display)) {

      struct line* line  = display->current_buffer->current_line;
      struct line* prev  = line->prev;

      display->cursor_line -= 1;

      if(prev)
        display->cursor_column = strlen(prev->data) - 1;

      buffer_join_line(display->current_buffer);

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

  return xstrcpy(user_input);
}

void
buffer_search_free(struct buffer* buffer)
{
  if(NULL != buffer->search) {
    free(buffer->search->str);
    free(buffer->search);
  }
  buffer->search = NULL;
}

void
buffer_search_alloc(struct buffer* buffer, char* search_term, int start_line)
{
  if(buffer->search)
    buffer_search_free(buffer);

  buffer->search = (struct search_state*) malloc(sizeof(struct search_state));
  buffer->search->prev_line = 0;
  buffer->search->prev_col = 0;
  buffer->search->line_number = start_line;
  buffer->search->line_column  = 0;
  buffer->search->str = xstrcpy(search_term);
}

bool
display_search(struct display* display,char* search,int current_line)
{
  LOG_DEBUG("Search for: %s ",search);

  struct buffer *buffer = display->current_buffer;
  buffer_search_alloc(buffer,search,current_line );

  bool found = buffer_search_forward(buffer,current_line);

  if(display->current_buffer->search->found)  { // found search
    struct search_state* search =  buffer->search;
    display_goto_position(display, search->line_number, search->line_column);
    move(display->cursor_line,display->cursor_column);
    display->mode = SEARCH_MODE;
    return found;
  } else {
    LOG_DEBUG("Cound not find %s\n",search);
    move(display->cursor_line,display->cursor_column);
    return false;
  }

}

bool
display_search_next(struct display* display, void* unused)
{
  struct buffer* buffer = display->current_buffer;
  struct search_state* search = buffer->search;

  if(NULL == search) {
    display->mode = COMMAND_MODE;
    return false;
  }

  bool found = display_search(display,search->str,search->line_number+1);

  if(!found) {
    display->mode = COMMAND_MODE;
    return false;
  }
  return found;
}

bool
display_search_prev(struct display* display,void* misc)
{
  return false;
}

bool
display_start_search(struct display* display,
                     void* misc)
{

  char* search_term = (char*) mode_line_input("/",display);
  if(search_term == NULL) {
    return true;
  }

  //TODO search backwards
  int start = display_line_number(display);
  display_search(display, search_term, start);
  return false;
}

bool
display_cmd_mode(struct display* display,void* misc)
{
  display->mode = COMMAND_MODE;
  return false;
}

struct display*
display_init(struct buffer* buffer,int height,int width)
{
  struct display* dis = (struct display*) malloc(sizeof(struct display));
  dis->cursor_column = 0;
  dis->cursor_line   = 0;
  dis->height = height;
  dis->width  = width;
  dis->mode_window   = newwin(height-1,width,height-2,0);
  dis->buffer_window = newwin(height-2,width,0,0);
  dis->mode = COMMAND_MODE;
  display_set_buffer(dis, buffer);
  return dis;
}

struct builtin_cmd {
  char* cmd;
  bool (*display_cmd)(struct display* display, void* misc);
};

const struct builtin_cmd commands[] =
  {
   {"w", &display_save},
   {"q", &display_quit},
   {"wq", &display_quit}
  };

bool
display_run_cmd(struct display* display,void* misc)
{
  char* cmd = (char*) mode_line_input(":",display);
  move(display->cursor_line,display->cursor_column);
  if(cmd == NULL) {
    return true;
  }
  LOG_DEBUG("read cmd: %s ",cmd);

  int i;
  for(i = 0; i < (int)ARRAY_SIZE(commands); i++) {
    if(strcmp(commands[i].cmd,cmd) == 0) {
      commands[i].display_cmd(display,misc);
    }
  }
  
  return true;
}

struct keymap_entry {
  char* cmd;
  bool keymap;
  bool (*display_cmd) (struct display* , void* );
};

struct mode {
  char* mode_line;
  const struct keymap_entry* keymap;
  int num_keys;
  bool (*default_cmd) (struct display*,void*);
};

const struct keymap_entry search_keymap[] =
{
 {NULL,false,   &display_to_command_mode},
 {"n" ,false,   &display_search_next},
 {"N" ,false,   &display_search_prev}
};

const struct keymap_entry insert_keymap[] =
{
 {NULL,  false, &display_insert_char},
 {"^C",  false, &display_to_command_mode},
 {"RET", false, &display_insert_cr},
 {"\b",  false, &display_insert_backspace},
 {"ESC",  false, &display_to_command_mode},
 {"\t",  false, &display_insert_tab}
};

const struct keymap_entry command_keymap[] =
{
 {NULL,false,&display_bleep},
 {"n",false, &display_search_next},
 {"<",false, &display_pg_up_begin},
 {">",false, &display_pg_down_begin},
 {"G",false, &display_pg_last},
 {"o",false, &display_open_line},
 {"j",false, &display_move_line_down},
 {"k",false, &display_move_line_up},
 {"l",false, &display_move_left},
 {"h",false, &display_move_right},
 {"$",false, &display_end_of_line},
 {"^",false, &display_begining_of_line},
 {"i",false, &display_to_insert_mode},
 {"J",false, &display_join},
 {"x",false, &display_delete_char},
 {"s",false, &display_save},
 {"d",false, &display_delete_line},
 {"/",false, &display_start_search},
 {":", false, &display_run_cmd},
 {"^C",false, &display_quit},
 {"q", false, &display_quit}
};

struct mode modes[] =
{
 {"INSERT" ,insert_keymap,  ARRAY_SIZE(insert_keymap),  &display_cmd_mode},
 {"COMMAND",command_keymap, ARRAY_SIZE(command_keymap), &display_cmd_mode},
 {"SEARCH" ,search_keymap,  ARRAY_SIZE(search_keymap),  &display_cmd_mode}
};

// preserve enum display_mode ordering
const struct keymap_entry* keymaps[] =
  {
   insert_keymap,
   command_keymap,
   search_keymap
  };

const struct keymap_entry*
keymap_find(const char* key,
            const struct keymap_entry keymap[],
            int size)
{
  int i = 0;
  for(i = 0; i < size; i++) {
    char* cmd =  keymap[i].cmd;
    if(cmd != NULL && strcmp(key,cmd) == 0) {
      return &(keymap[i]);
    }
  }
  // Use first entry in map as default
  return &(keymap[0]);
}

const struct keymap_entry*
keymap_find_by_char(char cur,
                    const struct keymap_entry keymap[],
                    int size)
{
  char char_cmd[5];
  if (3 == cur) {
    sprintf(char_cmd,"%s","^C");
  } else if (27 == cur) {
    sprintf(char_cmd,"%s","ESC");
  } else if (KEY_ENTER == cur || '\n' == cur) {
    sprintf(char_cmd,"%s","RET");
  } else if (KEY_BACKSPACE == cur  || 127 == cur || 8 == cur || '\b' == cur){
    sprintf(char_cmd,"%s","\b");
  } else {
    sprintf(char_cmd,"%c",cur);
  }

  return keymap_find( char_cmd, keymap, size);
}

/**
 * Show the mode-line at the bottom of the display.
 */
void
mode_line_show(struct display* display)
{
  struct buffer* cur = display->current_buffer;
  char mode_line[display->width-1];
  if(cur->current_line) {
    snprintf(mode_line,display->width-1,
             "-[%s name: %s, cursor:(%d,%d) len:%d num_lines:%d , mode:%s ][%d x %d] line:%s",
             cur->modified ? "**" : " ",
             cur->buf_name,
             display->cursor_line,
             display->cursor_column,
             (int)strlen(cur->current_line->data),
             cur->num_lines,
             modes[display->mode].mode_line,
             display->height,
             display->width,
             display->current_buffer->current_line->data);
  }

  wmove(display->mode_window,0,0);
  wprintw(display->mode_window,mode_line);
  wrefresh(display->mode_window);
}

void
start_display(struct buffer* buffer)
{
  char cur;
  int height, width;

  initscr();
  getmaxyx(stdscr, height, width);

  struct display* display =
    display_init(buffer,height,width);

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
      cur = getch(); // wait for input
      //
      LOG_DEBUG("display_loop: received [%c] \n",cur);
      struct mode mode = modes[display->mode];
      const struct keymap_entry* kmp = mode.keymap;
      const struct keymap_entry* entry =
        keymap_find_by_char(cur,kmp,mode.num_keys);

      LOG_DEBUG("%s-command found:%c\n",
                modes[display->mode].mode_line,cur);

      redisplay = entry->display_cmd(display,&cur);
    }
  }
  endwin();
}

int
main(int argc,char* argv[])
{

  XLOG = new Logger(); //logging_init();
  
  Display display;
  
  string bufferName("x.cc");
  string filePath("/home/aakarsh/src/c/x/x.cc");
  
  if(argc > 1) {     // add buffer to display
    bufferName = argv[1];
    filePath   = argv[1];
  }
  
  display +=  new Buffer(bufferName, filePath);

  display.start();

  //logging_end(XLOG);
  delete XLOG;
  return 0;
}

/**
 * Creates allocates copy of string on heap and returns a pointer, or
 * NULL on any error.
 */
char*
xstrcpy(char* str)
{
  if(NULL == str)
    return NULL;

  long length = strlen(str);
  char* retval = (char*) malloc ((length+1) * sizeof(char));

  if(NULL == retval)
    return NULL;

  strncpy(retval,str,(size_t)length);
  retval[length]='\0';

  return str;
}


/**
 * Concatenate two lines dropping terminating new line
 * of first newline if it has one.
 */
char*
strlinecat(char * l0,char* l1)
{
  char* line  = (char*) realloc(l0, strlen(l0) + strlen(l1));
  //
  if(!line)
    return NULL;
  //
  while(*(l0++) && *(l0)!='\n') ;
  //
  while((*l0++ =  *l1++)) ;
  //
  return line;
}

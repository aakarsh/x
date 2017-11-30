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

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>

#include <memory>

using namespace std;
class app;
class logger;

class app {
  friend class logger;

public:
  static bool debug_mode;
  static string debug_log_file;
  static logger* debug_logger;
  static logger& get_logger();
};

#ifdef DEBUG
bool app::debug_mode = true;
#else
bool app::debug_mode = false;
#endif
string app::debug_log_file = "x-debug.log";

const char* log_file ="x.log";

enum log_level { LOG_LEVEL_INFO, LOG_LEVEL_DEBUG};

class logger {
private:
  // static instance of logger.
  static logger* debug_logger;

public:
  log_level level;
  FILE* debug_file;
  ofstream debug_stream;
  static bool debug_mode;

  logger():
      level(LOG_LEVEL_INFO)
      ,debug_stream(app::debug_log_file,
                   std::ofstream::out)
  {
    if(app::debug_mode) {
      this->level = LOG_LEVEL_DEBUG;
      this->debug_file = fopen("foo.log","w+");
      this->debug_stream.open(app::debug_log_file,
                             std::ofstream::out);
    }
  }

  ostream& out(){
    return this->debug_stream;
  }

  logger& log(const string& str) {
    ostream &log = out();
    if(app::debug_mode) {
      log<<str<<endl;
    }
    log.flush();
    return *this;
  }

  ~logger() {
#ifdef DEBUG
    this->debug_stream.close();
    //this->debug_file.close();
    fclose(this->debug_file);
#endif
  }
};

logger* app::debug_logger;

logger& app::get_logger() {
  if(!app::debug_logger) {
    app::debug_logger = new logger();
  }
  return *debug_logger;
}

class x_line {
public:
  // Position relative to the file.
  long line_number;
  streampos file_position;
  long line_pos;

  // x_line data
  string data;

  x_line(int line_no,
       streampos fpos,
       int lpos) :
    line_number(line_no)
    ,file_position(fpos)
    ,line_pos(lpos) {}

  x_line(int line_no,
       streampos fpos,
       int lpos,
       string& data):
     line_number(line_no)
    ,file_position(fpos)
    ,line_pos(lpos)
    ,data(data){}

  x_line(): x_line(0,0,0) {}

  int size(){
    return this->data.size();
  }

};

class buf {

private:
  // List of buffer errors
  enum buffer_error { buffer_noerror,
                      buffer_file_errory,
                      buffer_no_file } ;

  // file backing this buffer
  const string file_path;

  // buffer name
  const string buffer_name;

  // file stream backing the buffer.
  fstream buffer_stream;

  buffer_error error_code;

  // size of buffer.
  off_t size;

  off_t fsize;
  bool modified;

  // current line
  int current_lineIndex ;

  // list of lines of the buffer.
  vector<x_line*> lines;

  typedef pair<pair<int,int>,pair<int,int>> border;

  // left-top , right-bottom
  border display_border;

  // index in buffer of current point
  int displayLine;

public:

  void set_display_border(border b) {
    this->display_border = b;
  }

  vector<x_line*>& get_lines() {
    return this->lines;
  }

  x_line* get_line(size_t idx) {
    if(idx >= this->lines.size()) {
      return nullptr;
    }
    return this->lines[idx];
  }

  bool is_modified() {
    return this->modified;
  }

  string get_buffer_name() {
    return this->buffer_name;
  }

  // Avoid defaults
  buf()  = delete;

 buf(const buf& buffer)  = delete;

  buf(string name, string path):
      file_path(path)
    , buffer_name(name)
    , buffer_stream(path, ios_base::in)  {

    if(buffer_stream.rdstate() && std::ifstream::failbit != 0) {
      error_code = buffer_noerror;
      return;
    }

    this->fill(buffer_stream);
  }

  ~buf() {
    // close open file
    buffer_stream.close();
    // free all lines.
    this->clear();
  }

  bool is_error_state() {
    return error_code != buffer_noerror;
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
    while(getline(in,line)) {// the whole file is read into mem
      x_line* cur =  new x_line(line_number, in.tellg(), 0, line);
      lines.push_back(cur);
    }
  }
};


class buf_list {

private:
  vector<buf*> buffers;
  buf* current_buffer;

public:
  buf_list() = default;

  buf_list(buf* first):
    current_buffer(first) {
    this->buffers.push_back(first);
  }

  buf_list& append(buf* buffer) {
    return this->append(*buffer);
  }

  buf_list& append(buf& buffer) {
    this->buffers.push_back(&buffer);
    this->current_buffer = &buffer;
    return *this;
  }


  int num_buffers()  {
    return this->buffers.size();
  }


  buf* get_current_buffer() {
    return this->current_buffer;
  }

};

class display_window {

private:
  int num_lines;
  int numColumns;
  int beginY;
  int beginX;

  WINDOW* window;

public:
  // using managed resource window
  display_window () = delete;
  display_window& operator=(const display_window& ) = delete;

  display_window(int nl, int nc,int by, int bx):
     num_lines(nl)
    ,numColumns(nc)
    ,beginY(by)
    ,beginX(bx) {

    window = newwin(num_lines,
                    numColumns,
                    beginY,
                    beginX);

    // start with cursor at beginning
    this->move_cursor(beginY,beginX);
  }

  int get_height() {
    return num_lines;
  };

  int get_width() {
    return numColumns;
  };

  void rewind() {
    move_cursor(beginY,beginX);
  }

  display_window& refresh() {
    wrefresh(window);
    return *this;
  }

  display_window& move_cursor(int y, int x){
    wmove(window,y,x);
    wrefresh(window);
    return *this;
  }

  display_window& display_line(int y, int x, const string& line) {
    wmove(window,y,x);
    wprintw(window,line.c_str());
    wrefresh(window);
    return *this;
  }

  display_window& display_line(string line) {
    wprintw(window,line.c_str());
    wrefresh(window);
    return *this;
  }

  display_window& clear() {
    wclear(window);
    wmove(window,0,0);
    wrefresh(window);
    return *this;
  }

  string read_input(const string &prompt ) {
    clear();
    display_line(0,0,prompt);
    char input[256];    
    echo();
    wgetnstr(window, input, 256);
    clear();
    noecho();
    return string(input);      
  }

  ~display_window() {
    delwin(window);
  }

};


class editor;
class editor_command;
class mode;
class Next_line;

enum  editor_mode { command_mode = 0,
                    insert_mode  = 1,
                    search_mode  = 2 };

typedef map<string,editor_command*> keymap;

class x_mode {
private:
  string mode_name;
  keymap mode_map;

public:
  x_mode(const string& name, const keymap &cmds) :
     mode_name(name)
    ,mode_map(cmds){}

  editor_command* lookup(const string& cmd) {
    return mode_map[cmd];
  }

  string& get_name() { return mode_name; }
};


class editor_command {
  friend class editor;
  vector<string> keys;
public:
  editor_command() {};
  editor_command(vector<string> & ks):keys(ks) {};

  vector<string> & getKeys() {
    return this->keys;
  }

  void keymap_add(keymap& map) {
    for(auto &key : getKeys()) {
      map.insert({key,this});
    }
  }

  virtual editor_mode run (editor &display, const string& cmd ) = 0;
};

// TODO: auto-gen
class move_pg : public editor_command {
public:
  move_pg(): editor_command() {};
  move_pg(vector<string> & ks): editor_command(ks) {};
  editor_mode run(editor& d, const string &cmd );
};

class search_fwd : public editor_command {
public:
  search_fwd(): editor_command() {};
  search_fwd(vector<string> & ks): editor_command(ks) {};
  editor_mode run(editor& d, const string &cmd );
};

class mv_point : public editor_command {
public:
  mv_point(): editor_command() {};
  mv_point(vector<string> & ks): editor_command(ks) {};
  editor_mode run(editor& d, const string &cmd);
};

class toggle : public editor_command {
public:
  toggle(): editor_command() {};
  toggle(vector<string> & ks): editor_command(ks) {};
  editor_mode run(editor& d, const string &cmd);
};

class editor {

private:
  vector<x_mode*> modes;

  int screen_height;
  int screen_width;

  int mode_padding = 1;
  editor_mode mode;

  display_window *mode_window;
  display_window *buffer_window;

  buf_list *buffers;

  bool redisplay; // trigger a buffer-redisplay of buffer
  bool quit;      // quit will cause the display loop to exit.

  typedef pair<int,int> point;

  point cursor;
  int start_line = 0;



public:
  bool line_number_show = false;

  enum move_dir { move_y = 0 , move_x };
  enum anchor_type { no_anchor = 0 ,
                     line_begin,
                     line_end,
                     page_begin,
                     page_end};

  editor() : buffers(new buf_list()) {

    // determine the screen
    initscr();

    // initialized the screen_height and screen_width
    getmaxyx(stdscr, this->screen_height, this->screen_width);

    this->mode_window    =
      new display_window(screen_height-1,
                        screen_width,
                        screen_height-mode_padding, // beginY
                        0);

    this->buffer_window  =
      new display_window(screen_height- mode_padding, // num lines
                        screen_width,                 // num cols
                        0,                            // beginY
                        0);                           // beginX

    keymap cmd_map;
    keymap search_map;

    vector<string> mv_point_keys {"j","^n","k",
                                  "^p" ,"^","0",
                                  "$","l","h",
                                  "^b","^f",
                                  "^a","^e"};
    mv_point* mv_pt_cmd = new mv_point(mv_point_keys);
    mv_pt_cmd->keymap_add(cmd_map);

    vector<string> move_pg_keys {">","<"," ","^v", "^V"};
    (new move_pg(move_pg_keys))->keymap_add(cmd_map);
    
    vector<string> toggle_keys {"."};
    (new toggle(toggle_keys))->keymap_add(cmd_map);

    vector<string> search_fwd_keys {"^s","/"};
    search_fwd* fwd = new search_fwd(search_fwd_keys);
    fwd->keymap_add(cmd_map);
    fwd->keymap_add(search_map);
    (new search_fwd(search_fwd_keys))->keymap_add(cmd_map);

    
    this->modes.push_back(new x_mode("CMD", cmd_map));
    //    this->modes.push_back(new x_mode("INSERT", ins_map));
    this->modes.push_back(new x_mode("SEARCH", search_map));
    this->mode = command_mode;
    raw();
    refresh();
  }

  int get_currrent_line_idx() {
    return
      this->start_line + this->cursor.first;
  }

  x_line* get_current_line() {
    int idx = this->get_currrent_line_idx();
    return this->get_current_buffer()->get_line(idx);
  }

  x_mode* get_current_mode() {
    return this->modes[mode];
  }

  void change_mode(editor_mode newMode) {
    if(newMode!= mode){
      mode = newMode;
    }
  }

  void run_cmd(const string& cmd) {
    if(cmd == "q") { // treat quit special for nwo
      this->quit = true;
    }else { // Need to look up command in the mode

      // don't do redisplay unless requested
      this->redisplay = false;

      x_mode* mode = this->get_current_mode();

      if (!mode)
        return;

      editor_command* editor_command = mode->lookup(cmd);
      if(!editor_command)
        return;

      editor_mode nextMode = editor_command->run(*this,cmd);
      this->change_mode(nextMode);
    }
  }

  void display_mode_line() {

    buf* current_buffer =
      this->buffers->get_current_buffer();

    string modified =
      current_buffer->is_modified() ? "*" : "-";

    stringstream mode_line;
    mode_line<<"["<<modified<<"] "<< current_buffer->get_buffer_name()
            <<" ------ " << "["<< this->get_current_mode()->get_name() <<"]";

    // rpait mode at 0 0
    this->mode_window->display_line(0, 0, mode_line.str());
  }

  string mode_read_input(const string & prompt) {
    string input =  this->mode_window->read_input(prompt);
    this->mode_window->display_line(0,0,input);
    return input;
  }

  buf* get_current_buffer() {
    return this->buffers->get_current_buffer();
  }

  void display_buffer() {
    app::get_logger().log("display_buffer");

    this->buffer_window->rewind();

    buf* buffer =
      this->buffers->get_current_buffer();

    vector<x_line*> & lines = buffer->get_lines();

    int line_count;

    for(auto line_ptr : lines) {

      if((line_count - start_line) >=
         this->buffer_window->get_height()) {
        break;
      }

      if(line_count < start_line) {
        line_count++;
        continue;
      }

      // iterate through the lines going to cursor poistion
      if(line_number_show){
        char ls[256];
        sprintf(ls,"%5d: ",line_count);
        this->buffer_window->display_line(string(ls));
      }
      
      this->buffer_window->display_line(line_ptr->data);
      this->buffer_window->display_line("\n");
      line_count++;
    }
    // rewind to beginning -
    this->buffer_window->rewind();
  }

  /**
   * box value withing limits with included
   * padding.
   */
  int box(int value,
          pair<int,int>  limits,
          pair<int,int>  padding) {
    int min = limits.first  - padding.first;
    int max = limits.second - padding.second;

    if(value  >= max)
      return max;
    else if(value <= min) {
      return min;
    } else {
      return value;
    }
  }

  point make_point(int y, int x){
    return make_pair(y,x);
  }

  point bol() {
    return make_pair(cursor.first,0);
  }

  point eol() {
    return make_pair(cursor.first, get_line_size());
  }

  int get_line_size(size_t idx) {
    if(idx >= 0 &&
       idx < this->get_current_buffer()->get_lines().size()) {

      x_line* cur = (this->get_current_buffer()->get_lines())[idx];

      if(cur) {
        return cur->size() - 1;
      }
    }

    return 0;
  }

  int get_line_size() {
    x_line* cur = this->get_current_line();
    if(cur) {
      return cur->size() - 1;
    }
    return 0;
  }

  point inc_point(point p, int inc, move_dir dir)
  {
    if(dir == move_y) {
      return make_point(box(p.first+inc,
                            {0, this->buffer_window->get_height()},
                            {0, this->mode_padding})
                        ,min(this->get_line_size(p.first+inc)+1 ,p.second));
    } else if(dir == move_x) { // need to compute size of incremented line
      return make_point(p.first,
                        box(p.second + inc,
                            {0, min(this->get_line_size(p.first)+1,
                                    this->buffer_window->get_width())},
                            {0, this->mode_padding}));
    } else{
      return cursor;
    }
  }

  void move_point(int inc, move_dir dir, anchor_type anchor ) {
    // compute increment relative to anchor
    if(anchor == no_anchor) {
      this->cursor = inc_point(cursor,inc,dir);
    } else if (anchor == line_begin) {
      this->cursor = inc_point(bol(),inc, editor::move_x);
    } else if (anchor == line_end) {
      this->cursor = inc_point(eol(),inc, editor::move_x);
    } else  {
      return;
    }
  }

  void move_page(int pg_inc) {
    int max_lines =
      this->get_current_buffer()->get_lines().size();

    int pg_size =
      this->buffer_window->get_height();

    int new_start_line =
      this->start_line + (pg_inc * screen_height);

    if(new_start_line <= 0 ) {
      this->start_line = 0;
    } else if(new_start_line >= max_lines){      
      this->start_line = max_lines - pg_size;
    }  else {      
      this->start_line = new_start_line;
    }

    mark_redisplay();
  }

  void mark_redisplay() {
    this->redisplay = true;
  }

  const string parse_cmd() {
    char cur = getch();
    string c(1,cur);

    vector<char> alphabet;
    char start = 'a';
    while(start < 'z')
      alphabet.push_back(start++);

    start = 'A';
    while(start < 'Z')
      alphabet.push_back(start++);

    for(auto & k : alphabet) {
      if(cur == (k  & 037)) { //character has been CTRL modified
        return (string("^") + string(1,k));
      } // ALT,SHIFT,...
    }
    return c;
  }

  void start() {
    this->quit = false;

    while(!this->quit) { // quit
      noecho();

      // mode line
      this->display_mode_line();

      // main buffer
      this->display_buffer();

      // move visible cursor
      this->display_cursor();

      // trigger command
      this->run_cmd(this->parse_cmd());

      // run the next command till redisplay becomes necessary
      while(!this->redisplay
            && !this->quit) {
        // get-input
        this->run_cmd(this->parse_cmd());


        // move the window to current place
        this->display_cursor();
        app::get_logger().log("cursor_line");
      }

      // need to reset to do a redisplay
      this->redisplay = false;
    }
    return;
  }

  void display_cursor(){
    move(this->cursor.first, this->cursor.second);
    refresh(); // refresh to see cursor.
  }

  /**
   * editor will handle the life cycle of th
   * buffer once it has been added to display's
   * buffer list.
   */
  void append_buffer(buf* buffer) {
    this->buffers->append(buffer);
  }

  ~editor(){
    endwin();
    // display manages buffers and its windows.
    delete buffers;
    delete mode_window;
    delete buffer_window;
  }
};


/**
 * point motion commands: make the bindings less explicit.
 */
editor_mode mv_point::run(editor& d, const string &cmd) {
  if(cmd == "j"|| cmd == "^n") { // move the cursor but dont do a redisplay
    d.move_point(1,editor::move_y, editor::no_anchor);
  } else if(cmd =="k" || cmd =="^p")  {
    d.move_point(-1,editor::move_y, editor::no_anchor);
  } else if(cmd == "l"|| cmd == "^f"){
    d.move_point(1,editor::move_x, editor::no_anchor);    // move the cursor but dont do a redisplay
  } else if(cmd =="h" || cmd =="^b")  {
    d.move_point(-1,editor::move_x, editor::no_anchor);
  } else if(cmd == "^" || cmd =="0" || cmd =="^a") {
    d.move_point(0,editor::move_x, editor::line_begin);
  } else if (cmd == "$" || cmd == "^e") {
    d.move_point(0,editor::move_x, editor::line_end);
  }

  return command_mode;
}

editor_mode move_pg::run(editor& d, const string &cmd) {
  if(cmd == " "|| cmd == ">" || cmd == "^v"){
    d.move_page(+1);    // move the cursor but dont do a redisplay
  } else if (cmd == "<" || cmd == "^V") {
    d.move_page(-1);
  }
  return command_mode;
}

editor_mode toggle::run(editor & d, const string& cmd) {
  if( cmd == "." ) {    
    if(d.line_number_show)
      d.line_number_show = false;
    else
      d.line_number_show = true;
  }
  
  d.mark_redisplay();
  return command_mode;
}

editor_mode search_fwd::run(editor & d, const string& cmd) {
  if( cmd == "/" ) {    
    string search_string  = d.mode_read_input(string("Search Forward :"));    
    d.mark_redisplay();
  } else if (cmd == "n") {
  }
  return search_mode;
}

int
main(int argc,char* argv[])
{

  //XLOG = new logger(); //logging_init();
  app::get_logger().log("x:started");
  editor editor;

  string buffer_name("x.cc");
  string file_path("/home/aakarsh/src/c/x/x.cc");

  if(argc > 1) {     // add buffer to editor
    buffer_name = argv[1];
    file_path   = argv[1];
  }

  editor.append_buffer(new buf(buffer_name, file_path));

  editor.start();

  //logging_end(XLOG);

  //  delete XLOG;
  delete &(app::get_logger()); // close log file
  return 0;
}


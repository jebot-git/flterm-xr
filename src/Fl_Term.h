//
// "$Id: Fl_Term.h 5175 2020-09-18 13:08:10 $"
//
// Fl_Term -- A terminal simulation widget
//
// Copyright 2017-2020 by Yongchao Fan.
//
// This library is free software distributed under GNU GPL 3.0,
// see the license at:
//
//     https://github.com/yongchaofan/tinyTerm2/blob/master/LICENSE
//
// Please report all bugs and problems on the following page:
//
//     https://github.com/yongchaofan/tinyTerm2/issues/new
//
#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include "host.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>
#include <string_view>
#include <optional>
#include <chrono>
#include "kitty-keyboard.hpp"

#ifndef _FL_TERM_H_
#define _FL_TERM_H_
class Fl_Term : public Fl_Widget {
    bool software_ = false;
	uint64_t c_attr;		//current character attribute(color)
	uint64_t save_attr;		//saved character attribute, used with save_x/save_y
	char *buff;			//buffer for characters, one byte per char
	uint64_t *attr;			//buffer for attributes, one style per UTF-8 byte
	int buff_size; 		//current buffer size, doubles at more_chars()
	int *line;			//buffer for starting position of each line
	int line_size;		//current max number of lines, doubles at more_lines()
	int size_x; 		//screen width in number of characters
	int size_y;			//screen height in number of characters
	int cursor_x;		//index to buff and attr for current insert position
	int	cursor_y;		//index to line buffer for current row of text
	int save_x;			//save_x/save_y also used to save and restore cursor
	int save_y;			//previous cursor_y when switch to alternate screen
	int screen_y;        // live parser origin, independent of the history view
    int view_top_ = -1; // -1 follows output; otherwise an absolute history row
    static constexpr int history_limit_ = 10000;
    struct MainScreen {
        char *text = nullptr;
        uint64_t *styles = nullptr;
        int *lines = nullptr;
        int bytes, rows, x, y, top, view, left, right, saved_x, saved_y;
        int margin_top, margin_bottom, received;
        uint64_t style, saved_style;
    } main_screen_;
    int viewTop() const { return view_top_ < 0 ? screen_y : std::min(view_top_, screen_y); }
    void reserveBuffer(int bytes, int rows);
    void trimHistory();
    void alternateBuffer(bool enable);
    void scrollScreenUp();
    int cellColumn(int row, int offset) const;
    int cellOffset(int row, int column) const;
    void replaceCells(int row, int first, int last, std::string_view text);
    void replaceRowBytes(int row, int first, int last, std::string_view text,
                         const uint64_t *styles = nullptr);
    void materializeRows();
    void putGlyph(std::string_view text);
    void scrollRegion(int top, int bottom, int amount);
    bool softwareCsi(std::string_view sequence);
    std::string utf8_pending_;
    kitty_keyboard::Modes keyboard_modes_;
	int roll_top;
	int roll_bot;		//the range of lines that will scroll in alterscreen
	int sel_left;
	int sel_right;		//begin and end of selection in scroll buffer
	float font_width;	//current font width
	int font_height;	//current font height
	int font_size;		//current font size, should equal to height
	int font_face;		//current font face
	std::atomic<bool> redraw_pending;
	std::mutex append_mtx;

	bool bEscape;		//escape sequence processing mode
	int ESC_idx;		//current index for ESC_code
	char ESC_code[256];	//cumulating the current escape sequence before process
	char tabstops[256];

	bool bInsert;		//insert mode, for inline editing for commands
	bool bGraphic;		//graphic character mode, for text mode drawing
	bool bCursor;		//display cursor or not
	bool bAppCursor;	//app cursor mode for vi
	bool bAltScreen;	//alternative screen for vi
	bool bBracket;		//bracketed paste mode
	bool bWraparound;
	bool bOriginMode;

    char control_string; // OSC, DCS, SOS, PM or APC; consumed through its terminator.
    bool control_escape;
    std::string control_payload;
	char sTitle[256];	//window title set by host

	char sPrompt[32];	//wait for sPrompt before next command when scripting
	int iPrompt;		//length of sPrompt
	bool bPrompt;		//if sPrompt was found after the last append

	int iTimeOut;		//time out in seconds while waiting for sPrompt
	int recv0;			//cursor_x at the start of last command
	int xmlIndent;		//used by putxml
	int xmlTagIsOpen;	//used by putxml

	bool bDND;			//if a FL_PASTE is result of drag&drop
	bool bWait;			//waitfor() function is waiting for string in buffer
	bool bEcho;			//if local echo is active
	bool bScriptRun;
	bool bScriptPause;

	std::atomic<bool> bGets;//gets() function is waiting for return bing pressed
	std::atomic<bool> bReturn;//true if return has been pressed during gets()
	int cursor;			//gets receive buffer index
	char keys[64];		//gets receive buffer
	bool bPassword;		//if gets() is wating for password, no echo if yes

	char *LogFileName;
	FILE *fpLogFile;
	HOST *host;
    std::array<uint8_t,3> default_foreground_{231,235,241}, default_background_{22,28,35};

protected:
	void draw();
	void next_line();
	void buff_clear(int offset, int len);
	void termsize(int cols, int rows);
	void screen_clear(int m0);
	void check_cursor_y();
	void append( const char *buf, int len );
	void put_xml(const char *buf, int len);
	const unsigned char *vt100_Escape(const unsigned char *buf, int cnt);
	const unsigned char *telnet_options(const unsigned char *buf, int cnt);

public:
    struct Run { int row, column, columns; std::string text; uint64_t style; bool selected; };
    struct Snapshot { std::vector<Run> runs; int cursorColumn, cursorRow; bool cursor; };
private:
    std::optional<Snapshot> synchronized_snapshot_;
    std::chrono::steady_clock::time_point synchronized_until_;
    Snapshot liveSnapshot();
public:
    Snapshot snapshot();
    void cellSize(int width, int height);
    void grid(int cols, int rows);
    void scrollRows(int amount);
    void scrollToBottom();
    int historyRows() const { return bAltScreen ? 0 : screen_y; }
    int scrollOffset() const { return screen_y - viewTop(); }
    void pasteText(const std::string &text);
    void selectCells(int column, int row, bool begin);
    void selectAll();
    std::string selectionText();
    bool applicationCursor() const { return bAppCursor; }
    bool bracketedPaste() const { return bBracket; }
    bool alternateScreen() const { return bAltScreen; }
    unsigned keyboardFlags() const { return keyboard_modes_.flags(bAltScreen); }
    void defaultColors(std::array<uint8_t,3> foreground,std::array<uint8_t,3> background) {
        default_foreground_=foreground;default_background_=background;
    }
	Fl_Term(int X,int Y,int W,int H,const char* L=0, bool software=false);
	~Fl_Term();
	void clear();
	int  handle(int e);
	void resize(int X, int Y, int W, int H);
	void textfont(Fl_Font fontface);
	void textsize(int fontsize);
	bool pending(){ return redraw_pending; }
	void pending(bool p) { redraw_pending=p; }
	const char *title() { return sTitle; }
	const char *hostname() { return host->name(); }

	int sizeX() { return size_x; }
	int sizeY() { return size_y; }
	char *logg() { return LogFileName; }
	void logg(const char *fn);
	void save(const char *fn);
	void srch(const char *word);

	int connect(HOST *newhost, const char **preply);
	bool live() { return host->live(); }
	void puts(const char *buf, int len);
	void write(const char *buf, int len);
	char *gets(const char *prompt, int echo);
	void disconn();
	void disp(const char *buf) { append(buf, strlen(buf)); }
	void send(const char *buf) { write(buf, strlen(buf)); }

	void learn_prompt();
	int  mark_prompt();
	int  waitfor_prompt();
	int command(const char *cmd, const char **preply);


	void copier(char *files);
	void scripter(char *cmds);
	void run_script(const char *script);
	bool script_running() { return bScriptRun; }
	bool pause_script();
	void quit_script();
};
#endif

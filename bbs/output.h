/**************************************************************************/
/*                                                                        */
/*                              WWIV Version 5.x                          */
/*             Copyright (C)1998-2016, WWIV Software Services             */
/*                                                                        */
/*    Licensed  under the  Apache License, Version  2.0 (the "License");  */
/*    you may not use this  file  except in compliance with the License.  */
/*    You may obtain a copy of the License at                             */
/*                                                                        */
/*                http://www.apache.org/licenses/LICENSE-2.0              */
/*                                                                        */
/*    Unless  required  by  applicable  law  or agreed to  in  writing,   */
/*    software  distributed  under  the  License  is  distributed on an   */
/*    "AS IS"  BASIS, WITHOUT  WARRANTIES  OR  CONDITIONS OF ANY  KIND,   */
/*    either  express  or implied.  See  the  License for  the specific   */
/*    language governing permissions and limitations under the License.   */
/*                                                                        */
/**************************************************************************/

#ifndef __INCLUDED_WSTREAMBUFFER_H__
#define __INCLUDED_WSTREAMBUFFER_H__

#include <iostream>
#include <streambuf>
#include <ios>
#include <string>
#include <utility>
#include <vector>

class outputstreambuf : public std::streambuf {
 public:
  outputstreambuf();
  ~outputstreambuf();
  virtual std::ostream::int_type overflow(std::ostream::int_type c);
  virtual std::streamsize xsputn(const char *text, std::streamsize numChars);
};

class RemoteIO;
class LocalIO;

class SavedLine {
public:
  SavedLine(std::vector<std::pair<char, uint8_t>> l, int c, const std::string& e): line(l), color(c), endofline(e) {}
  std::vector<std::pair<char, uint8_t>> line;
  int color;
  std::string endofline;
};

class Output : public std::ostream {
 protected:
  outputstreambuf buf;
  LocalIO *local_io_;
  RemoteIO *comm_;

 public:
  Output() :
#if defined(_WIN32)
    buf(),
#endif
    std::ostream(&buf) {
    init(&buf);
  }
  virtual ~Output() {}

  void SetLocalIO(LocalIO *local_io) { local_io_ = local_io; }
  LocalIO* localIO() const { return local_io_; }

  void SetComm(RemoteIO *comm) { comm_ = comm; }
  RemoteIO* remoteIO() const { return comm_; }

  void Color(int wwivColor);
  void ResetColors();
  void GotoXY(int x, int y);
  void nl(int nNumLines = 1);
  void bs();
  /* This sets the current color (both locally and remotely) to that
   * specified (in IBM format).
   */
  void SystemColor(int nColor);
  void litebar(const char *fmt, ...);
  /** Backspaces from the current cursor position to the beginning of a line */
  void backline();

  /**
   * Moves the cursor to the end of the line using ANSI sequences.  If the user
   * does not have ansi, this this function does nothing.
   */
  void clreol();

  /**
   * Clears the local and remote screen using ANSI (if enabled), otherwise DEC 12
   */
  void cls();

  /**
   * This will make a reverse-video prompt line i characters long, repositioning
   * the cursor at the beginning of the input prompt area.  Of course, if the
   * user does not want ansi, this routine does nothing.
   */
  void mpl(int numOfChars);

  /**
   * This function outputs a string of characters to the screen (and remotely
   * if applicable).  The com port is also checked first to see if a remote
   * user has hung up
   */
  int bputs(const std::string& text);
  int bprintf(const char *fmt, ...);

  int bputch(char c, bool use_buffer = false);
  void flush();
  void rputch(char ch, bool use_buffer = false);
  void rputs(const char *text);
  char getkey();
  bool RestoreCurrentLine(const SavedLine& line);
  SavedLine SaveCurrentLine();
  void dump();
  void clear_endofline() { endofline_.clear(); }

private:
  std::string bputch_buffer_;
  std::vector<std::pair<char, uint8_t>> current_line_;
  std::string endofline_;
};

namespace wwiv {
template<class charT, class traits>
std::basic_ostream<charT, traits>&
endl(std::basic_ostream<charT, traits>& strm) {
  strm.write("\r\n", 2);
  return strm;
}
}

#endif  // __INCLUDED_WSTREAMBUFFER_H__

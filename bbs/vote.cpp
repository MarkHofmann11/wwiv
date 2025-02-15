/**************************************************************************/
/*                                                                        */
/*                              WWIV Version 5.x                          */
/*             Copyright (C)1998-2022, WWIV Software Services             */
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
#include "bbs/vote.h"

#include "bbs/application.h"
#include "bbs/bbs.h"
#include "bbs/mmkey.h"
#include "common/input.h"
#include "common/output.h"
#include "core/file.h"
#include "core/strings.h"
#include "fmt/printf.h"
#include "sdk/config.h"
#include "sdk/filenames.h"
#include "sdk/status.h"
#include <string>

using namespace wwiv::core;
using namespace wwiv::strings;

static void print_quest(int mapp, int map[21]) {
  votingrec v;

  bout.cls();
  bout.litebar(StrCat(a()->config()->system_name(), " Voting Questions"));
  bool abort = false;
  File voteFile(FilePath(a()->config()->datadir(), VOTING_DAT));
  if (!voteFile.Open(File::modeReadOnly | File::modeBinary)) {
    return;
  }

  for (int i = 1; i <= mapp && !abort; i++) {
    voteFile.Seek(map[i] * sizeof(votingrec), File::Whence::begin);
    voteFile.Read(&v, sizeof(votingrec));

    auto buffer = fmt::sprintf("|#6%c |#2%2d|#7) |#1%s",
             a()->user()->votes(map[i]) ? ' ' : '*', i, v.question);
    bout.bpla(buffer, &abort);
  }
  voteFile.Close();
  bout.nl();
  if (abort) {
    bout.nl();
  }
}

static bool print_question(int i, int ii) {
  votingrec v;

  File voteFile(FilePath(a()->config()->datadir(), VOTING_DAT));
  if (!voteFile.Open(File::modeReadOnly | File::modeBinary)) {
    return false;
  }
  voteFile.Seek(ii * sizeof(votingrec), File::Whence::begin);
  voteFile.Read(&v, sizeof(votingrec));
  voteFile.Close();
  bool abort = false;

  if (!v.numanswers) {
    return false;
  }

  bout.cls();
  bout.bpla(fmt::format("|#5Voting question #{}", i), &abort);
  bout.Color(1);
  bout.bpla(v.question, &abort);
  bout.nl();
  int t = 0;
  voting_response vr{};
  for (int i1 = 0; i1 < v.numanswers; i1++) {
    vr = v.responses[i1];
    t += vr.numresponses;
  }

  a()->status_manager()->reload_status();
  const auto b = fmt::sprintf("|#9Users voting: |#2%4.1f%%\r\n",
                       static_cast<double>(t) /
                           static_cast<double>(a()->status_manager()->user_count()) * 100.0);
  bout.bpla(b, &abort);
  int t1 = (t) ? t : 1;
  bout.bpla(" |#20|#9) |#9No Comment", &abort);
  std::set<char> odc;
  for (int i3 = 0; i3 < v.numanswers && !abort; i3++) {
    vr = v.responses[ i3 ];
    if (((i3 + 1) % 10) == 0) {
      odc.insert('0' + static_cast<char>((i3 + 1) / 10));
    }
    const auto l = fmt::sprintf("|#2%2d|#9) |#9%-60s   |#3%4d  |#1%5.1f%%",
            i3 + 1, vr.response, vr.numresponses,
            static_cast<float>(vr.numresponses) / static_cast<float>(t1) * 100.0);
    bout.bpla(l , &abort);
  }
  bout.nl();
  if (abort) {
    bout.nl();
  }
  return !abort;
}

static void vote_question(int i, int ii) {
  votingrec v;

  bool pqo = print_question(i, ii);
  if (a()->user()->restrict_vote() || a()->sess().effective_sl() <= 10 || !pqo) {
    return;
  }

  File file(FilePath(a()->config()->datadir(), VOTING_DAT));
  if (!file.Open(File::modeReadOnly | File::modeBinary)) {
    return;
  }
  file.Seek(ii * sizeof(votingrec), File::Whence::begin);
  file.Read(&v, sizeof(votingrec));
  file.Close();

  if (!v.numanswers) {
    return;
  }

  std::string message = "|#9Your vote: |#1";
  if (a()->user()->votes(ii)) {
    message.append(v.responses[ a()->user()->votes(ii) - 1 ].response);
  } else {
    message +=  "No Comment";
  }
  bout <<  message;
  bout.nl(2);
  bout << "|#5Change it? ";
  if (!bin.yesno()) {
    return;
  }

  bout << "|#5Which (0-" << static_cast<int>(v.numanswers) << ")? ";
  bout.mpl(2);
  auto empty_set = std::set<char>();
  const auto ans = mmkey(empty_set);
  int i1 = to_number<int>(ans);
  if (i1 > v.numanswers) {
    i1 = 0;
  }
  if (i1 == 0 && ans != "0") {
    return;
  }

  if (!file.Open(File::modeReadOnly | File::modeBinary)) {
    return;
  }
  file.Seek(ii * sizeof(votingrec), File::Whence::begin);
  file.Read(&v, sizeof(votingrec));

  if (!v.numanswers) {
    file.Close();
    return;
  }
  if (a()->user()->votes(ii)) {
    v.responses[ a()->user()->votes(ii) - 1 ].numresponses--;
  }
  a()->user()->votes(ii, i1);
  if (i1) {
    v.responses[ a()->user()->votes(ii) - 1 ].numresponses++;
  }
  file.Seek(ii * sizeof(votingrec), File::Whence::begin);
  file.Write(&v, sizeof(votingrec));
  file.Close();
  bout.nl(2);
}

void vote() {
  votingrec v;

  File voteFile(FilePath(a()->config()->datadir(), VOTING_DAT));
  if (!voteFile.Open(File::modeReadOnly | File::modeBinary)) {
    return;
  }

  if (const auto n = static_cast<int>(voteFile.length() / sizeof(votingrec)) - 1; n < 20) {
    v.question[0] = 0;
    v.numanswers = 0;
    for (int i = n; i < 20; i++) {
      voteFile.Write(&v, sizeof(votingrec));
    }
  }

  std::set<char> odc;

  int map[21], mapp = 0;
  for (int i1 = 0; i1 < 20; i1++) {
    voteFile.Seek(i1 * sizeof(votingrec), File::Whence::begin);
    voteFile.Read(&v, sizeof(votingrec));
    if (v.numanswers) {
      map[++mapp] = i1;
      if ((mapp % 10) == 0) {
        odc.insert('0' + static_cast<char>(mapp / 10));
      }
    }
  }
  voteFile.Close();

  if (mapp == 0) {
    bout << "\r\n\n|#6No voting questions currently.\r\n\n";
    return;
  }
  bool done = false;
  do {
    print_quest(mapp, &map[0]);
    bout.nl();
    bout << "|#9(|#2Q|#9=|#2Quit|#9) Voting: |#2# |#9: ";
    bout.mpl(2);
    std::string answer= mmkey(odc);
    int nQuestionNum = to_number<int>(answer);
    if (nQuestionNum > 0 && nQuestionNum <= mapp) {
      vote_question(nQuestionNum, map[ nQuestionNum ]);
    } else if (answer == "Q") {
      done = true;
    } else if (answer == "?") {
      print_quest(mapp, &map[0]);
    }
  } while (!done && !a()->sess().hangup());
}

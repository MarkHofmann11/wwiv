/**************************************************************************/
/*                                                                        */
/*                              WWIV Version 5.0x                         */
/*             Copyright (C)1998-2014, WWIV Software Services             */
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
#include <cstdint>
#include <memory>

#include "bbs/input.h"
#include "bbs/common.h"
#include "bbs/instmsg.h"
#include "bbs/menu.h"
#include "bbs/menusupp.h"
#include "bbs/printfile.h"
#include "bbs/wwiv.h"
#include "core/stl.h"
#include "core/strings.h"
#include "core/textfile.h"
#include "core/wwivassert.h"

static user_config *pSecondUserRec;         // Userrec2 style setup
static int nSecondUserRecLoaded;            // Whos config is loaded

using std::string;
using std::unique_ptr;
using namespace wwiv::strings;
using namespace wwiv::stl;

// Local function prototypes
bool LoadMenuSetup(int user_number);
bool ValidateMenuSet(const char *pszMenuDir);
void StartMenus();
bool CheckMenuSecurity(const MenuHeader* pHeader, bool bCheckPassword);
void MenuExecuteCommand(MenuInstanceData* menu_data, const string& command);
void LogUserFunction(const MenuInstanceData* menu_data, const string& command, MenuRec* pMenu);
void PrintMenuPrompt(MenuInstanceData* menu_data);
bool AMIsNumber(const string& command);
void QueryMenuSet();
void WriteMenuSetup(int user_number);
void UnloadMenuSetup();
const string GetCommand(const MenuInstanceData* menu_data);
bool CheckMenuItemSecurity(MenuRec* pMenu, bool bCheckPassword);
const char *MenuDoParenCheck(const char *pszSrc, int bMore, const char *porig);
const char *MenuGetParam(const char *pszSrc, char *pszParam);
const char *MenuSkipSpaces(const char *pszSrc);
void InterpretCommand(MenuInstanceData* menu_data, const char *pszScript);

static bool CheckMenuPassword(const string& original_password) {
  const string expected_password = (original_password == "*SYSTEM") ? syscfg.systempw : original_password;

  bout.nl();
  string actual_password;
  input_password("|#2SY: ", &actual_password, 20);
  return actual_password == expected_password;
}

void mainmenu() {
  if (pSecondUserRec) {
    free(pSecondUserRec);
    pSecondUserRec = nullptr;
  }
  pSecondUserRec = static_cast<user_config *>(malloc(sizeof(user_config)));
  if (!pSecondUserRec) {
    return;
  }

  while (!hangup) {
    StartMenus();
  }

  free(pSecondUserRec);
  pSecondUserRec = nullptr;
  nSecondUserRecLoaded = 0;
}

void StartMenus() {
  unique_ptr<MenuInstanceData> menu_data(new MenuInstanceData{});
  menu_data->reload = true;                    // force loading of menu

  if (!LoadMenuSetup(session()->usernum)) {
    strcpy(pSecondUserRec->szMenuSet, "wwiv"); // default menu set name
    pSecondUserRec->cHotKeys = HOTKEYS_ON;
    pSecondUserRec->cMenuType = MENUTYPE_REGULAR;
    WriteMenuSetup(session()->usernum);
  }
  while (menu_data->reload && !hangup) {
    menu_data->menu_file.release();

    if (!LoadMenuSetup(session()->usernum)) {
      LoadMenuSetup(1);
      ConfigUserMenuSet();
    }
    if (!ValidateMenuSet(pSecondUserRec->szMenuSet)) {
      ConfigUserMenuSet();
    }

    menu_data->Menus(pSecondUserRec->szMenuSet, "main"); // default starting menu
  }
}

void MenuInstanceData::Menus(const string& menuDirectory, const string& menuName) {
  path = menuDirectory;
  menu = menuName;

  if (Open()) {
    if (header.nNumbers == MENU_NUMFLAG_DIRNUMBER && udir[0].subnum == -1) {
      bout << "\r\nYou cannot currently access the file section.\r\n\n";
      Close();
      return;
    }
    // if flagged to display help on entrance, then do so
    if (session()->user()->IsExpert() && header.nForceHelp == MENU_HELP_ONENTRANCE) {
      DisplayHelp();
    }

    while (!hangup && !finished) {
      PrintMenuPrompt(this);
      const string command = GetCommand(this);
      MenuExecuteCommand(this, command);
    }
  } else if (IsEqualsIgnoreCase(menuName.c_str(), "main")) {     // default menu name
    hangup = true;
  }
  Close();
}

MenuInstanceData::MenuInstanceData() {}

MenuInstanceData::~MenuInstanceData() {
  Close();
}

void MenuInstanceData::Close() {
  menu_file.release();
  insertion_order_.clear();
  nAmountRecs = 0;
  menu_command_map_.clear();
}

//static
const std::string MenuInstanceData::create_menu_filename(
    const std::string& path, const std::string& menu, const std::string& extension) {
  return StrCat(GetMenuDirectory(), path, File::pathSeparatorString, menu, ".", extension);
}

const string MenuInstanceData::create_menu_filename(const string& extension) const {
  return MenuInstanceData::create_menu_filename(path, menu, extension);
}

bool MenuInstanceData::CreateMenuMap() {
  insertion_order_.clear();
  int nAmount = menu_file->GetLength() / sizeof(MenuRec);

  for (uint16_t nRec = 1; nRec < nAmount; nRec++) {
    MenuRec menu;
    menu_file->Seek(nRec * sizeof(MenuRec), File::seekBegin);
    menu_file->Read(&menu, sizeof(MenuRec));

    menu_command_map_.emplace(menu.szKey, menu);
    if (nRec != 0 && !(menu.nFlags & MENU_FLAG_DELETED)) {
      insertion_order_.push_back(menu.szKey);
    }
  }
  return true;
}

bool MenuInstanceData::Open() {
  nAmountRecs = 0;
  Close();

  // Open up the main data file
  menu_file.reset(new File(create_menu_filename("mnu")));
  menu_file->Open(File::modeBinary | File::modeReadOnly, File::shareDenyNone);

  // Find out how many records there are
  if (menu_file->IsOpen()) {
    long lSize = menu_file->GetLength();
    nAmountRecs = static_cast<uint16_t>(lSize / sizeof(MenuRec));
  } else {
    // Unable to open menu
    MenuSysopLog("Unable to open Menu");
    return false;
  }

  // Read the header (control) record into memory
  menu_file->Seek(0L, File::seekBegin);
  menu_file->Read(&header, sizeof(MenuHeader));

  // Version numbers can be checked here.
  if (!CreateMenuMap()) {
    MenuSysopLog("Unable to create menu index.");
    nAmountRecs = 0;
    return false;
  }

  // Open/Rease/Close Prompt file
  File filePrompt(create_menu_filename("pro"));
  if (filePrompt.Open(File::modeBinary | File::modeReadOnly, File::shareDenyNone)) {
    long size = filePrompt.GetLength();
    unique_ptr<char[]> s(new char[size + 1]);
    size = filePrompt.Read(s.get(), size);
    s.get()[size] = '\0';
    char* sp = strstr(s.get(), ".end.");
    if (sp) {
      *sp = '\0';
    }
    prompt.assign(s.get());
    filePrompt.Close();
  }
  if (!CheckMenuSecurity(&header, true)) {
    MenuSysopLog("< Menu Sec");
    return false;
  }
  if (header.szScript[0]) {
    InterpretCommand(this, header.szScript);
  }
  return true;
}

static const string GetHelpFileName(const MenuInstanceData* menu_data) {
  if (session()->user()->HasAnsi()) {
    if (session()->user()->HasColor()) {
      const string filename = menu_data->create_menu_filename("ans");
      if (File::Exists(filename)) {
        return filename;
      }
    }
    const string filename = menu_data->create_menu_filename("b&w");
    if (File::Exists(filename)) {
      return filename;
    }
  }
  return menu_data->create_menu_filename("msg");
}

void MenuInstanceData::DisplayHelp() const {
  const string filename = GetHelpFileName(this);
  if (!printfile(filename, true)) {
    GenerateMenu();
  }
}

bool CheckMenuSecurity(const MenuHeader* pHeader, bool bCheckPassword) {
  if ((pHeader->nFlags & MENU_FLAG_DELETED) ||
      (session()->GetEffectiveSl() < pHeader->nMinSL) ||
      (session()->user()->GetDsl() < pHeader->nMinDSL)) {
    return false;
  }

  // All AR bits specified must match
  for (short int x = 0; x < 16; x++) {
    if (pHeader->uAR & (1 << x)) {
      if (!session()->user()->HasArFlag(1 << x)) {
        return false;
      }
    }
  }

  // All DAR bits specified must match
  for (short int x = 0; x < 16; x++) {
    if (pHeader->uDAR & (1 << x)) {
      if (!session()->user()->HasDarFlag(1 << x)) {
        return (session()->user()->GetDsl() < pHeader->nMinDSL);
      }
    }
  }

  // If any restrictions match, then they arn't allowed
  for (short int x = 0; x < 16; x++) {
    if (pHeader->uRestrict & (1 << x)) {
      if (session()->user()->HasRestrictionFlag(1 << x)) {
        return (session()->user()->GetDsl() < pHeader->nMinDSL);
      }
    }
  }

  if ((pHeader->nSysop && !so()) ||
      (pHeader->nCoSysop && !cs())) {
    return false;
  }

  if (pHeader->szPassWord[0] && bCheckPassword) {
    if (!CheckMenuPassword(pHeader->szPassWord)) {
      return false;
    }
  }
  return true;
}

bool MenuInstanceData::LoadMenuRecord(const std::string& command, MenuRec** pMenu) {
  // If we have 'numbers set the sub #' turned on then create a command to do so if a # is entered.
  if (AMIsNumber(command)) {
    if (header.nNumbers == MENU_NUMFLAG_SUBNUMBER) {
      memset(pMenu, 0, sizeof(MenuRec));
      sprintf((*pMenu)->szExecute, "SetSubNumber %d", atoi(command.c_str()));
      return true;
    }
    if (header.nNumbers == MENU_NUMFLAG_DIRNUMBER) {
      memset(pMenu, 0, sizeof(MenuRec));
      sprintf((*pMenu)->szExecute, "SetDirNumber %d", atoi(command.c_str()));
      return true;
    }
  }

  if (contains(menu_command_map_, command)) {
    *pMenu = &menu_command_map_.at(command);
    if (CheckMenuItemSecurity(*pMenu, true)) {
      return true;
    } else {
      MenuSysopLog(StrCat("< item security : ", command));
      return false;
    }
    return true;
  }
  return false;
}

bool LoadMenuRecord(MenuInstanceData* menu_data, const string& command, MenuRec* pMenu) {
  MenuRec *tmp;
  if (menu_data->LoadMenuRecord(command, &tmp)) {
    memcpy(pMenu, tmp, sizeof(MenuRec));
  }
  return false;
}

void MenuExecuteCommand(MenuInstanceData* menu_data, const string& command) {
  MenuRec* menu;

  if (menu_data->LoadMenuRecord(command, &menu)) {
    LogUserFunction(menu_data, command, menu);
    InterpretCommand(menu_data, menu->szExecute);
  } else {
    LogUserFunction(menu_data, command, menu);
  }
}

void LogUserFunction(const MenuInstanceData* menu_data, const string& command, MenuRec* pMenu) {
  switch (menu_data->header.nLogging) {
  case MENU_LOGTYPE_KEY:
    sysopchar(command.c_str());
    break;
  case MENU_LOGTYPE_COMMAND:
    sysoplog(pMenu->szExecute);
    break;
  case MENU_LOGTYPE_DESC:
    sysoplog(pMenu->szMenuText[0] ? pMenu->szMenuText : pMenu->szExecute);
    break;
  case MENU_LOGTYPE_NONE:
  default:
    break;
  }
}

void MenuSysopLog(const string msg) {
  const string log_message = StrCat("*MENU* : ", msg);
  sysoplog(log_message);
  bout << log_message << wwiv::endl;
}

void PrintMenuPrompt(MenuInstanceData* menu_data) {
  if (!session()->user()->IsExpert() || menu_data->header.nForceHelp == MENU_HELP_FORCE) {
    menu_data->DisplayHelp();
  }
  TurnMCIOn();
  if (!menu_data->prompt.empty()) {
    bout << menu_data->prompt;
  }
  TurnMCIOff();
}

void TurnMCIOff() {
  if (!(syscfg.sysconfig & sysconfig_enable_mci)) {
    g_flags |= g_flag_disable_mci;
  }
}

void TurnMCIOn() {
  g_flags &= ~g_flag_disable_mci;
}

bool AMIsNumber(const string& command) {
  if (!command.length()) {
    return false;
  }

  for (const auto& ch : command) {
    if (!isdigit(ch)) {
      return false;
    }
  }
  return true;
}

void ConfigUserMenuSet() {
  if (session()->usernum != nSecondUserRecLoaded) {
    if (!LoadMenuSetup(session()->usernum)) {
      LoadMenuSetup(1);
    }
  }

  nSecondUserRecLoaded = session()->usernum;

  bout.cls();
  printfile(MENUWEL_NOEXT);
  bool bDone = false;
  while (!bDone && !hangup) {
    bout << "   |#1WWIV |#6Menu |#1Editor|#0\r\n\r\n";
    bout << "|#21|06) |#1Menuset      |06: |15" << pSecondUserRec->szMenuSet << wwiv::endl;
    bout << "|#22|06) |#1Use hot keys |06: |15" << (pSecondUserRec->cHotKeys == HOTKEYS_ON ? "Yes" : "No ") <<
                       wwiv::endl;
    bout.nl();
    bout << "|#9[|0212? |08Q|02=Quit|#9] :|#0 ";

    char chKey = onek("Q12?");

    switch (chKey) {
    case 'Q':
      bDone = 1;
      break;

    case '1': {
      ListMenuDirs();
      bout.nl(2);
      bout << "|15Enter the menu set to use : |#0";
      string menuSetName;
      inputl(&menuSetName, 8);
      if (ValidateMenuSet(menuSetName.c_str())) {
        wwiv::menus::MenuDescriptions descriptions(GetMenuDirectory());
        bout.nl();
        bout << "|#1Menu Set : |#2" <<  menuSetName.c_str() << "  -  |15" << descriptions.description(menuSetName) << wwiv::endl;
        bout << "|#5Use this menu set? ";
        if (noyes()) {
          strcpy(pSecondUserRec->szMenuSet, menuSetName.c_str());
          break;
        }
      }
      bout.nl();
      bout << "|#8That menu set does not exists, resetting to the default menu set" << wwiv::endl;
      pausescr();
      if (pSecondUserRec->szMenuSet[0] == '\0') {
        strcpy(pSecondUserRec->szMenuSet, "wwiv");
      }
    }
    break;

    case '2':
      pSecondUserRec->cHotKeys = !pSecondUserRec->cHotKeys;
      break;

    case '3':
      pSecondUserRec->cMenuType = !pSecondUserRec->cMenuType;
      break;

    case '?':
      printfile(MENUWEL_NOEXT);
      continue;                           // bypass the below cls()
    }

    bout.cls();
  }

  // If menu is invalid, it picks the first one it finds
  if (!ValidateMenuSet(pSecondUserRec->szMenuSet)) {
    if (session()->num_languages > 1 && session()->user()->GetLanguage() != 0) {
      bout << "|#6No menus for " << languages[session()->user()->GetLanguage()].name
           << " language.";
      input_language();
    }
  }

  WriteMenuSetup(session()->usernum);

  MenuSysopLog(StringPrintf("Menu in use : %s - %s - %s", pSecondUserRec->szMenuSet,
          pSecondUserRec->cHotKeys == HOTKEYS_ON ? "Hot" : "Off", pSecondUserRec->cMenuType == MENUTYPE_REGULAR ? "REG" : "PD"));
  bout.nl(2);
}


void QueryMenuSet() {
  user_config tmpcfg;
  bool bSecondUserRecLoaded = false;

  if (!pSecondUserRec) {
    bSecondUserRecLoaded = true;
    pSecondUserRec = &tmpcfg;
  }

  if (session()->usernum != nSecondUserRecLoaded) {
    if (!LoadMenuSetup(session()->usernum)) {
      LoadMenuSetup(1);
    }
  }

  nSecondUserRecLoaded = session()->usernum;
  ValidateMenuSet(pSecondUserRec->szMenuSet);

  bout.nl(2);
  if (pSecondUserRec->szMenuSet[0] == 0) {
    strcpy(pSecondUserRec->szMenuSet, "WWIV");
  }
  bout << "|#7Configurable menu set status:\r\n\r\n";
  bout << "|#8Menu in use  : |#9" << pSecondUserRec->szMenuSet << wwiv::endl;
  bout << "|#8Hot keys are : |#9" << (pSecondUserRec->cHotKeys == HOTKEYS_ON ? "On" : "Off") << wwiv::endl;
  bout.nl();
  bout << "|#7Would you like to change these? (N) ";
  if (yesno()) {
    ConfigUserMenuSet();
  }

  if (bSecondUserRecLoaded) {
    pSecondUserRec = nullptr;
    nSecondUserRecLoaded = 0;
  }
}

bool ValidateMenuSet(const char *pszMenuDir) {
  if (session()->usernum != nSecondUserRecLoaded) {
    if (!LoadMenuSetup(session()->usernum)) {
      LoadMenuSetup(1);
    }
  }
  nSecondUserRecLoaded = session()->usernum;
  // ensure the entry point exists
  return File::Exists(GetMenuDirectory(pszMenuDir), "main.mnu");
}

bool LoadMenuSetup(int user_number) {
  if (!pSecondUserRec) {
    MenuSysopLog("Mem Error");
    return false;
  }
  UnloadMenuSetup();

  if (!user_number) {
    return false;
  }
  File userConfig(syscfg.datadir, CONFIG_USR);
  if (!userConfig.Exists()) {
    return false;
  }
  WUser user;
  application()->users()->ReadUser(&user, user_number);
  if (userConfig.Open(File::modeReadOnly | File::modeBinary)) {
    userConfig.Seek(user_number * sizeof(user_config), File::seekBegin);

    int len = userConfig.Read(pSecondUserRec, sizeof(user_config));
    userConfig.Close();

    if (len != sizeof(user_config) || !IsEqualsIgnoreCase(reinterpret_cast<char*>(pSecondUserRec->name), user.GetName())) {
      memset(pSecondUserRec, 0, sizeof(user_config));
      strcpy(reinterpret_cast<char*>(pSecondUserRec->name), user.GetName());
      return 0;
    }
    nSecondUserRecLoaded = user_number;
    return true;
  }
  return false;
}

void WriteMenuSetup(int user_number) {
  if (!user_number) {
    return;
  }

  WUser user;
  application()->users()->ReadUser(&user, user_number);
  strcpy(pSecondUserRec->name, user.GetName());

  File userConfig(syscfg.datadir, CONFIG_USR);
  if (!userConfig.Open(File::modeReadWrite | File::modeBinary | File::modeCreateFile)) {
    return;
  }

  userConfig.Seek(user_number * sizeof(user_config), File::seekBegin);
  userConfig.Write(pSecondUserRec, sizeof(user_config));
  userConfig.Close();
}

void UnloadMenuSetup() {
  nSecondUserRecLoaded = 0;
  memset(pSecondUserRec, 0, sizeof(user_config));
}

const string GetCommand(const MenuInstanceData* menu_data) {
  if (pSecondUserRec->cHotKeys == HOTKEYS_ON) {
    if (menu_data->header.nNumbers == MENU_NUMFLAG_DIRNUMBER) {
      write_inst(INST_LOC_XFER, udir[session()->GetCurrentFileArea()].subnum, INST_FLAGS_NONE);
      return string(mmkey(1, WSession::mmkeyFileAreas));
    } else if (menu_data->header.nNumbers == MENU_NUMFLAG_SUBNUMBER) {
      write_inst(INST_LOC_MAIN, usub[session()->GetCurrentMessageArea()].subnum, INST_FLAGS_NONE);
      return string(mmkey(0, WSession::mmkeyMessageAreas));
    } else {
      odc[0] = '/';
      odc[1] = '\0';
      return string(mmkey(2));
    }
  } else {
    string text;
    input(&text, 50);
    return string(text);
  }
}

bool CheckMenuItemSecurity(MenuRec * pMenu, bool bCheckPassword) {
  // if deleted, return as failed
  if ((pMenu->nFlags & MENU_FLAG_DELETED) ||
      (session()->GetEffectiveSl() < pMenu->nMinSL) ||
      (session()->GetEffectiveSl() > pMenu->iMaxSL && pMenu->iMaxSL != 0) ||
      (session()->user()->GetDsl() < pMenu->nMinDSL) ||
      (session()->user()->GetDsl() > pMenu->iMaxDSL && pMenu->iMaxDSL != 0)) {
    return false;
  }

  // All AR bits specified must match
  for (int x = 0; x < 16; x++) {
    if (pMenu->uAR & (1 << x)) {
      if (!session()->user()->HasArFlag(1 << x)) {
        return false;
      }
    }
  }

  // All DAR bits specified must match
  for (int x = 0; x < 16; x++) {
    if (pMenu->uDAR & (1 << x)) {
      if (!session()->user()->HasDarFlag(1 << x)) {
        return false;
      }
    }
  }

  // If any restrictions match, then they arn't allowed
  for (int x = 0; x < 16; x++) {
    if (pMenu->uRestrict & (1 << x)) {
      if (session()->user()->HasRestrictionFlag(1 << x)) {
        return false;
      }
    }
  }

  if ((pMenu->nSysop && !so()) ||
      (pMenu->nCoSysop && !cs())) {
    return false;
  }

  if (pMenu->szPassWord[0] && bCheckPassword) {
    if (!CheckMenuPassword(pMenu->szPassWord)) {
      return false;
    }
  }

  // If you made it past all of the checks
  // then you may execute the menu record
  return true;
}

namespace wwiv {
namespace menus {

MenuDescriptions::MenuDescriptions(const std::string& menupath) :menupath_(menupath) {
  TextFile file(menupath, DESCRIPT_ION, "rt");
  if (file.IsOpen()) {
    string s;
    while (file.ReadLine(&s)) {
      StringTrim(&s);
      if (s.empty()) {
        continue;
      }
      string::size_type space = s.find(' ');
      if (space == string::npos) {
        continue;
      }
      string menu_name = s.substr(0, space);
      string description = s.substr(space + 1);
      StringLowerCase(&menu_name);
      StringLowerCase(&description);
      descriptions_.emplace(menu_name, description);
    }
  }
}

MenuDescriptions::~MenuDescriptions() {
}

const std::string MenuDescriptions::description(const std::string& name) const {
  if (contains(descriptions_, name)) {
    return descriptions_.at(name);
  }
  return "";
}

bool MenuDescriptions::set_description(const std::string& name, const std::string& description) {
  descriptions_[name] = description;

  TextFile file(menupath_, DESCRIPT_ION, "wt");
  if (!file.IsOpen()) {
    return false;
  }

  for (const auto& iter : descriptions_) {
    file.WriteFormatted("%s %s", iter.first.c_str(), iter.second.c_str());
  }
  return true;
}

}
}

const string GetMenuDirectory(const string menuPath) {
  return StrCat(GetMenuDirectory(), menuPath, File::pathSeparatorString);
}

const string GetMenuDirectory() {
  return StrCat(session()->language_dir, "menus", File::pathSeparatorString);
}

void MenuInstanceData::GenerateMenu() const {
  bout.Color(0);
  bout.nl();

  int iDisplayed = 0;
  if (header.nNumbers != MENU_NUMFLAG_NOTHING) {
    bout.bprintf("|#1%-8.8s  |#2%-25.25s  ", "[#]", "Change Sub/Dir #");
    ++iDisplayed;
  }
  for (const auto& key : insertion_order_) {
    if (!contains(menu_command_map_, key)) {
      continue;
    }
    MenuRec menu = menu_command_map_.at(key);
    if (CheckMenuItemSecurity(&menu, false) &&
        menu.nHide != MENU_HIDE_REGULAR &&
        menu.nHide != MENU_HIDE_BOTH) {
      char szKey[30];
      if (strlen(menu.szKey) > 1 && menu.szKey[0] != '/' && pSecondUserRec->cHotKeys == HOTKEYS_ON) {
        sprintf(szKey, "//%s", menu.szKey);
      } else {
        sprintf(szKey, "[%s]", menu.szKey);
      }
      bout.bprintf("|#1%-8.8s  |#2%-25.25s  ", szKey,
                    menu.szMenuText[0] ? menu.szMenuText : menu.szExecute);
      if (iDisplayed % 2) {
        bout.nl();
      }
      ++iDisplayed;
    }
  }
  if (IsEquals(session()->user()->GetName(), "GUEST")) {
    if (iDisplayed % 2) {
      bout.nl();
    }
    bout.bprintf("|#1%-8.8s  |#2%-25.25s  ",
                                      pSecondUserRec->cHotKeys == HOTKEYS_ON ? "//APPLY" : "[APPLY]",
                                      "Guest Account Application");
    ++iDisplayed;
  }
  bout.nl(2);
  return;
}

// MenuParseLine(szSrc, szCmd, szParam1, szParam2)
//
// szSrc    = Line to parse
// szCmd    = Returns the command to be executed
// szParam1 = 1st parameter - if it exists
// szParam2 = 2cd paramater - if it exists
//
// return value is where to continue parsing this line
//
// szSrc to be interpreted can be formated like this
// either  cmd param1 param2
// or     cmd(param1, param2)
//   multiple lines are seperated with the ~ key
//   enclose multi word text in quotes
//
const char *MenuParseLine(const char *pszSrc, char *pszCmd, char *pszParam1, char *pszParam2) {
  const char *porig = pszSrc;

  pszCmd[0] = 0;
  pszParam1[0] = 0;
  pszParam2[0] = 0;

  pszSrc = MenuSkipSpaces(pszSrc);
  while (pszSrc[0] == '~' && !hangup) {
    ++pszSrc;
    pszSrc = MenuSkipSpaces(pszSrc);
  }

  int nLen = 0;
  while (isalnum(*pszSrc) && !hangup) {
    if (nLen < 30) {
      pszCmd[nLen++] = *pszSrc;
    }
    ++pszSrc;
  }
  pszCmd[nLen] = 0;

  pszSrc = MenuSkipSpaces(pszSrc);

  bool bParen = false;
  if (*pszSrc == '(') {
    ++pszSrc;
    bParen = true;
    pszSrc = MenuSkipSpaces(pszSrc);
  }
  pszSrc = MenuGetParam(pszSrc, pszParam1);
  pszSrc = MenuSkipSpaces(pszSrc);

  if (bParen) {
    pszSrc = MenuDoParenCheck(pszSrc, 1, porig);
  }

  pszSrc = MenuGetParam(pszSrc, pszParam2);
  pszSrc = MenuSkipSpaces(pszSrc);

  if (bParen) {
    pszSrc = MenuDoParenCheck(pszSrc, 0, porig);
  }

  pszSrc = MenuSkipSpaces(pszSrc);

  if (pszSrc[0] != 0 && pszSrc[0] != '~') {
    MenuSysopLog("Expected EOL");
    MenuSysopLog(porig);
  }
  return pszSrc;
}

const char *MenuDoParenCheck(const char *pszSrc, int bMore, const char *porig) {
  if (pszSrc[0] == ',') {
    if (bMore == 0) {
      MenuSysopLog("Too many paramaters in line of code");
      MenuSysopLog(porig);
    }
    ++pszSrc;
    pszSrc = MenuSkipSpaces(pszSrc);
  } else if (pszSrc[0] == ')') {
    ++pszSrc;
    pszSrc = MenuSkipSpaces(pszSrc);
    if (pszSrc[0] != 0 && pszSrc[0] != '~') {
      MenuSysopLog("Invalid Code, exptected EOL after close parentheses");
      MenuSysopLog(porig);
    }
  } else if (pszSrc[0] == 0 || pszSrc[0] == '~') {
    MenuSysopLog("Unexpected end of line (wanted a close parentheses)");
  }

  return pszSrc;
}

const char *MenuGetParam(const char *pszSrc, char *pszParam) {
  pszSrc = MenuSkipSpaces(pszSrc);
  pszParam[0] = 0;

  if (pszSrc[0] == 0 || pszSrc[0] == '~') {
    return pszSrc;
  }

  int nLen = 0;
  if (*pszSrc == '"') {
    ++pszSrc;
    nLen = 0;
    while (*pszSrc != '"' && *pszSrc != 0) {
      if (nLen < 50) {
        pszParam[nLen++] = *pszSrc;
      }
      ++pszSrc;
    }
    if (*pszSrc) {                           // We should either be on nullptr or the CLOSING QUOTE
      ++pszSrc;                              // If on the Quote, advance one
    }
    pszParam[nLen] = '\0';
  } else {
    nLen = 0;
    while (*pszSrc != ' ' && *pszSrc != 0 && *pszSrc != '~') {
      if (nLen < 50) {
        pszParam[nLen++] = *pszSrc;
      }
      ++pszSrc;
    }
    pszParam[nLen] = '\0';
  }
  return pszSrc;
}

const char *MenuSkipSpaces(const char *pszSrc) {
  while (isspace(pszSrc[0]) && pszSrc[0] != '\0') {
    ++pszSrc;
  }
  return pszSrc;
}

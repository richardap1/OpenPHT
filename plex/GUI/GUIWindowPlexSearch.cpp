/*
 *      Copyright (C) 2011 Plex
 *      http://www.plexapp.com
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread/mutex.hpp>

#include "CharsetConverter.h"
#include "Key.h"
#include "TimeUtils.h"
#include "FileItem.h"
#include "GUIBaseContainer.h"
#include "GUIInfoManager.h"
#include "GUILabelControl.h"
#include "GUIWindowPlexSearch.h"
#include "GUIUserMessages.h"
#include "PlexContentWorker.h"
#include "PlexDirectory.h"
#include "Client/PlexServerManager.h"
#include "Settings.h"
#include "Util.h"
#include "PlexUtils.h"
#include "input/XBMC_vkeys.h"
#include "PlexApplication.h"
#include "Client/PlexTimelineManager.h"
#include "GUIEditControl.h"
#include "GUIMessage.h"
#include "ApplicationMessenger.h"
#include "PlexThemeMusicPlayer.h"

#define CTL_LABEL_EDIT       310
#define CTL_BUTTON_BACKSPACE 8
#define CTL_BUTTON_CLEAR     10
#define CTL_BUTTON_SPACE     32

#define SEARCH_DELAY         750

using namespace XFILE;
using namespace std;

///////////////////////////////////////////////////////////////////////////////
CGUIWindowPlexSearch::CGUIWindowPlexSearch() : CGUIWindow(WINDOW_PLEX_SEARCH, "PlexSearch.xml"), m_searchTimer(this)
{
  m_editControl = NULL;
  m_loadType = LOAD_ON_GUI_INIT;
  m_lastFocusedContainer = -1;
  m_lastFocusedItem = -1;

  m_resultMap[PLEX_DIR_TYPE_MOVIE] = 9001;
  m_resultMap[PLEX_DIR_TYPE_SHOW] = 9002;
  m_resultMap[PLEX_DIR_TYPE_EPISODE] = 9004;
  m_resultMap[PLEX_DIR_TYPE_ARTIST] = 9008;
  m_resultMap[PLEX_DIR_TYPE_ALBUM] = 9009;
  m_resultMap[PLEX_DIR_TYPE_TRACK] = 9010;
  m_resultMap[PLEX_DIR_TYPE_CLIP] = 9012;
  m_resultMap[PLEX_DIR_TYPE_ROLE] = 9013;
}

///////////////////////////////////////////////////////////////////////////////
CGUIWindowPlexSearch::~CGUIWindowPlexSearch()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////
CStdString CGUIWindowPlexSearch::GetString()
{
  if (m_editControl)
    return m_editControl->GetLabel2();
  return "";
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowPlexSearch::OnTimeout()
{
  CGUIMessage msg(GUI_MSG_LABEL_RESET, GetID(), GetID());
  CApplicationMessenger::Get().SendGUIMessage(msg, GetID(), false);

  CStdString str = GetString();
  CPlexServerManager::CPlexServerOwnedModifier modifier = g_guiSettings.GetBool("myplex.searchsharedlibraries") ? CPlexServerManager::SERVER_ALL : CPlexServerManager::SERVER_OWNED;
  PlexServerList list = g_plexApplication.serverManager->GetAllServers(modifier);
  BOOST_FOREACH(CPlexServerPtr server, list)
  {
    CURL u = server->BuildPlexURL("/search");
    u.SetOption("query", str);
    m_currentSearchString.push_back(CJobManager::GetInstance().AddJob(new CPlexDirectoryFetchJob(u), this, CJob::PRIORITY_LOW));
    CLog::Log(LOGDEBUG, "CGUIWindowPlexSearch::OnTimeout searching %s", server->toString().c_str());
    MarkDirtyRegion();
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowPlexSearch::UpdateSearch()
{
  CStdString str = GetString();

  if (InProgress() && m_currentSearchString != str)
  {
    CLog::Log(LOGDEBUG, "CGUIWindowPlexSearch::UpdateSearch canceling all searches!");

    BOOST_FOREACH(int64_t id, m_currentSearchId)
      CJobManager::GetInstance().CancelJob(id);

    m_currentSearchString.clear();
  }

  if (!str.empty())
  {
    if (m_searchTimer.IsRunning())
      m_searchTimer.Restart();
    else
      m_searchTimer.Start(SEARCH_DELAY, false);
  }
  else
    Reset();
}

///////////////////////////////////////////////////////////////////////////////
bool CGUIWindowPlexSearch::OnAction(const CAction &action)
{
  if (m_editControl)
  {
    if (action.GetID() >= KEY_ASCII)
    {
      bool ret = m_editControl->OnAction(action);
      UpdateSearch();
      return ret;
    }
    else if ((action.GetID() == ACTION_BACKSPACE || action.GetID() == ACTION_NAV_BACK) && !GetString().empty())
    {
      bool ret = m_editControl->OnAction(CAction(ACTION_BACKSPACE));
      UpdateSearch();
      return ret;
    }
  }
  return CGUIWindow::OnAction(action);
}

///////////////////////////////////////////////////////////////////////////////
bool CGUIWindowPlexSearch::OnMessage(CGUIMessage& message)
{

  if (message.GetMessage() == GUI_MSG_CLICKED)
  {
    if (OnClick(message.GetSenderId(), message.GetParam1()))
      return true;
  }
  else if (message.GetMessage() == GUI_MSG_LABEL_RESET && message.GetControlId() == GetID())
  {
    Reset();
    return true;
  }
  else if (message.GetMessage() == GUI_MSG_SEARCH_UPDATE)
  {
    ProcessResults((CFileItemList*)message.GetPointer());
    return true;
  }

  bool ret = CGUIWindow::OnMessage(message);

  if (message.GetMessage() == GUI_MSG_WINDOW_INIT)
  {
    InitWindow();
    g_plexApplication.timelineManager->SetTextFieldFocused(true, m_editControl->GetDescription(), GetString());
  }

  if (message.GetMessage() == GUI_MSG_WINDOW_DEINIT)
    g_plexApplication.timelineManager->SetTextFieldFocused(false, m_editControl->GetDescription());

  if (message.GetMessage() == GUI_MSG_SET_TEXT && message.GetControlId() == CTL_LABEL_EDIT)
    UpdateSearch();

  return ret;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool CGUIWindowPlexSearch::OnClick(int senderId, int action)
{
  if (senderId == CTL_BUTTON_BACKSPACE)
  {
    OnAction(CAction(ACTION_BACKSPACE));
  }
  else if (senderId == CTL_BUTTON_CLEAR)
  {
    m_editControl->SetLabel2("");
    Reset();
  }
  else if (senderId == CTL_BUTTON_SPACE)
  {
    CStdString str = GetString();
    str += " ";
    m_editControl->SetLabel2(str);
    UpdateSearch();
  }
  else if (senderId >= 65 && senderId <= 100)
  {
    char c;

    if (senderId <= 90)
      c = 'A' + (senderId - 65);
    else
      c = '0' + (senderId - 91);

    CStdString str = GetString();
    str += c;
    m_editControl->SetLabel2(str);
    UpdateSearch();
  }
  else if (senderId >= 9001)
  {
    CGUIBaseContainer* container = (CGUIBaseContainer*)GetControl(senderId);
    if (container)
    {
      std::vector<CGUIListItemPtr> items = container->GetItems();
      if (items.size() > container->GetSelectedItem())
      {
        CGUIListItemPtr item = items.at(container->GetSelectedItem());
        if (item && item->IsFileItem())
        {
          CFileItemPtr fileItem = boost::static_pointer_cast<CFileItem>(item);

          m_lastFocusedContainer = container->GetID();
          m_lastFocusedItem = container->GetSelectedItem();

          if (!item->m_bIsFolder)
          {
            if (action == ACTION_PLAYER_PLAY ||
                (!PlexUtils::CurrentSkinHasPreplay() ||
                 fileItem->GetPlexDirectoryType() == PLEX_DIR_TYPE_TRACK ||
                 fileItem->GetPlexDirectoryType() == PLEX_DIR_TYPE_PHOTO))
            {
              PlayPlexItem(fileItem);
              return true;
            }
          }

          if (action == ACTION_SELECT_ITEM)
          {
            m_navHelper.navigateToItem(fileItem);
            return true;
          }
        }
      }
    }

    return false;
  }
  else
    return false;


  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowPlexSearch::HideAllLists()
{
  std::pair<int, int> intPair;
  BOOST_FOREACH(intPair, m_resultMap)
  {
    SET_CONTROL_HIDDEN(intPair.second);
    SET_CONTROL_HIDDEN(intPair.second - 2000);
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowPlexSearch::Reset()
{
  std::pair<int, int> intPair;
  BOOST_FOREACH(intPair, m_resultMap)
  {
    CGUIBaseContainer* container = (CGUIBaseContainer*)GetControl(intPair.second);
    if (container)
    {
      CGUIMessage msg(GUI_MSG_LABEL_RESET, GetID(), container->GetID());
      OnMessage(msg);
    }

    SET_CONTROL_HIDDEN(intPair.second);
    SET_CONTROL_HIDDEN(intPair.second - 2000);
  }

  if (GetFocusedControlID() >= 9000)
  {
    CGUIMessage msg(GUI_MSG_SETFOCUS, GetID(), 70);
    OnMessage(msg);
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowPlexSearch::InitWindow()
{
  CGUIEditControl* ctrl = (CGUIEditControl*)GetControl(CTL_LABEL_EDIT);
  if (ctrl)
  {
    m_editControl = ctrl;
    m_editControl->SetOnlyCaps(true);
  }
  else
    CLog::Log(LOGWARNING, "CGUIWindowPlexSearch::InitWindow Couldn't find editlabel with ID %d", CTL_LABEL_EDIT);

  if (m_lastFocusedContainer == -1)
    HideAllLists();
  else
  {
    g_plexApplication.themeMusicPlayer->playForItem(CFileItem());

    CGUIMessage msg(GUI_MSG_SETFOCUS, GetID(), m_lastFocusedContainer);
    OnMessage(msg);

    CONTROL_SELECT_ITEM(m_lastFocusedContainer, m_lastFocusedItem);
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowPlexSearch::ProcessResults(CFileItemList* results)
{
  std::map<int, CFileItemListPtr> mappedRes;
  for (int i = 0; i < results->Size(); i ++)
  {
    CFileItemPtr item = results->Get(i);
    if (item && m_resultMap.find(item->GetPlexDirectoryType()) != m_resultMap.end())
    {
      CFileItemListPtr list;
      if (mappedRes.find(item->GetPlexDirectoryType()) == mappedRes.end())
      {
        list = CFileItemListPtr(new CFileItemList);
        mappedRes[item->GetPlexDirectoryType()] = list;
      }
      else
        list = mappedRes[item->GetPlexDirectoryType()];

      CLog::Log(LOGDEBUG, "CGUIWindowPlexSearch::ProcessResults Added item %s to %d", item->GetLabel().c_str(), item->GetPlexDirectoryType());
      list->Add(item);
    }
    else if (item)
    {
      CLog::Log(LOGDEBUG, "CGUIWindowPlexSearch::ProcessResults got item with type %d which we can't handle!", item->GetPlexDirectoryType());
    }
  }

  std::pair<int, CFileItemListPtr> pair;
  BOOST_FOREACH(pair, mappedRes)
  {
    CGUIBaseContainer* container = (CGUIBaseContainer*)GetControl(m_resultMap[pair.first]);
    if (container)
    {
      CFileItemListPtr list = pair.second;
      std::vector<CGUIListItemPtr> cList = container->GetItems();

      int i = 0;
      BOOST_FOREACH(CGUIListItemPtr item, cList)
        list->AddFront(boost::static_pointer_cast<CFileItem>(item), i++);

      CGUIMessage msg(GUI_MSG_LABEL_BIND, GetID(), container->GetID(), 0, 0, list.get());
      OnMessage(msg);

      SET_CONTROL_VISIBLE(container->GetID());
      SET_CONTROL_VISIBLE(container->GetID() - 2000);
    }
    else
      CLog::Log(LOGDEBUG, "CGUIWindowPlexSearch::ProcessResults Could not find container %d", m_resultMap[pair.first]);
  }

  delete results;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void CGUIWindowPlexSearch::OnJobComplete(unsigned int jobID, bool success, CJob *job)
{
  CPlexDirectoryFetchJob *fjob = static_cast<CPlexDirectoryFetchJob*>(job);
  if (fjob && success)
  {
    CFileItemList* list = new CFileItemList;
    list->Copy(fjob->m_items);

    CGUIMessage msg(GUI_MSG_SEARCH_UPDATE, GetID(), GetID(), 0, 0, list);
    CApplicationMessenger::Get().SendGUIMessage(msg, WINDOW_PLEX_SEARCH, false);
  }
}

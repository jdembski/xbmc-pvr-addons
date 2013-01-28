

#include "N7Xml.h"
#include "tinyxml/tinyxml.h"
#include "tinyxml/XMLUtils.h"
#include <sstream>


using namespace ADDON;
using namespace PLATFORM;

bool CCurlFile::Get(const std::string &strURL, std::string &strResult)
{
  void* fileHandle = XBMC->OpenFile(strURL.c_str(), 0);
  if (fileHandle)
  {
    char buffer[1024];
    while (XBMC->ReadFileString(fileHandle, buffer, 1024))
      strResult.append(buffer);
    XBMC->CloseFile(fileHandle);
    return true;
  }
  return false;
}

N7Xml::N7Xml(void) : m_connected(false)
{
  m_iUpdateTimer = 0;
  list_channels();

  CreateThread();
}

N7Xml::~N7Xml(void)
{
  //if (IsRunning())
    StopThread();
 
  m_epg.clear(); 
  m_channels.clear();
}

void N7Xml::replace(std::string& str, const std::string& from, const std::string& to)
{
  std::string::size_type pos = 0;
  
  while ((pos = str.find(from, pos)) != std::string::npos)
  {
    str.replace(pos, from.length(), to);
    pos += to.length();
  }
}

int N7Xml::GetEPGData()
{
  XBMC->Log(LOG_DEBUG, "%s Fetching EPG Data from N7 backend!", __FUNCTION__);
  int iCurrentChannelId = -1;
  m_epg.clear();

  CStdString strUrl;
  strUrl.Format("http://%s:%d/n7chepg.xml", g_strHostname.c_str(), g_iPort);
  CStdString strXML;
  
  CCurlFile http;
  if(!http.Get(strUrl, strXML))
  {
    XBMC->Log(LOG_DEBUG, "%s - Could not open connection to N7 backend.",__FUNCTION__);
    return false;
  }

  XBMC->Log(LOG_DEBUG, "%s Fetched XML, length:%d", __FUNCTION__, strXML.length());
  
  TiXmlDocument xmlDoc;
  if (!xmlDoc.Parse(strXML.c_str()))
  {
    XBMC->Log(LOG_DEBUG, "Unable to parse XML: %s at line %d", xmlDoc.ErrorDesc(), xmlDoc.ErrorRow());
    return false;
  }
  
  TiXmlHandle hDoc(&xmlDoc);
  TiXmlElement* pElem;
  TiXmlHandle hRoot(0);
  
  pElem = hDoc.FirstChildElement("rss").Element();
  
  if (!pElem)
  {
    XBMC->Log(LOG_DEBUG, "Could not find <rss> element");
    return false;
  }
  
  hRoot = TiXmlHandle(pElem);
  pElem = hRoot.FirstChildElement("channel").Element();
  
  if (!pElem)
  {
    XBMC->Log(LOG_DEBUG, "Could not find <channel> element");
    return false;
  }
  
  iCurrentChannelId = GetUniqueChannelId(atoi(pElem->Attribute("id")));
  
  XBMC->Log(LOG_DEBUG, "%s - Parsing EPG for channel:'%d'", __FUNCTION__, iCurrentChannelId);
 
  hRoot=TiXmlHandle(pElem);
  
  TiXmlElement* pNode = hRoot.FirstChildElement("item").Element();
  
  if (!pNode)
  {
    XBMC->Log(LOG_DEBUG, "Could not find <item> element");
    return false;
  }
  
  for (; pNode != NULL; pNode = pNode->NextSiblingElement("item"))
  {
    CStdString strTmp;
    EPGEntry epg;
    
    epg.iChannelNumber = iCurrentChannelId;
    if (!XMLUtils::GetString(pNode, "title", strTmp))
      continue;
    
    epg.strTitle = strTmp;
    
    if (!XMLUtils::GetString(pNode, "description", strTmp))
      continue;
    
    epg.strPlot = strTmp;
    
    if (!XMLUtils::GetString(pNode, "programme", strTmp))
      continue;
       
    replace(strTmp, " +0000\"", "");
    replace(strTmp, "event id= ", ";");
    replace(strTmp, "channel id=", ";");
    replace(strTmp, "stop=\"", ";");
    replace(strTmp, "start=\"", "");
    replace(strTmp, "\"", "");
    replace(strTmp, " ", "");

    XBMC->Log(LOG_DEBUG, "%s: Programme string: %s", __FUNCTION__, strTmp.c_str());  
    
    time_t start;
    time_t stop;
    unsigned int iEventId;
    
    
    GetInfoFromProgrammeString(strTmp, start, stop, iEventId);
    
    epg.startTime = start;
    epg.endTime = stop;
    epg.iEventId = iEventId;
    
    m_epg.push_back(epg);
    
    XBMC->Log(LOG_DEBUG, "%s - EPG Title:'%s', ChannelNumber:'%d', Start:%d, End:%d, ID:%d", __FUNCTION__, epg.strTitle.c_str(), epg.iChannelNumber, start, stop, iEventId);
  }
  
  return iCurrentChannelId;
}

bool N7Xml::GetInfoFromProgrammeString(const std::string& strProgramm, time_t& start, time_t& stop, unsigned int& iEventID)
{
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int seconds = 0;
 
  // First get the start date-string
  int pos = strProgramm.find(";",0);
  std::string strTmp = strProgramm.substr(0, pos);
  
  year = atoi(strTmp.substr(0,4).c_str());
  month = atoi(strTmp.substr(4,2).c_str());
  day = atoi(strTmp.substr(6,2).c_str());
  hour = atoi(strTmp.substr(8,2).c_str());
  minute = atoi(strTmp.substr(10,2).c_str());
  seconds = atoi(strTmp.substr(12,2).c_str());
  
  struct tm tmpTime;
  
  tmpTime.tm_year = year - 1900;
  tmpTime.tm_mon = month - 1;
  tmpTime.tm_mday = day;
  tmpTime.tm_hour = hour+1;
  tmpTime.tm_min = minute;
  tmpTime.tm_sec = seconds;
  
  start = mktime(&tmpTime);
  
  XBMC->Log(LOG_DEBUG, "Start: %s Y:%d, M:%d, D:%d, H:%d, M:%d, S:%d (%s) time_t(%d)", __FUNCTION__, year, month, day, hour, minute, seconds, strTmp.c_str(), start);
  
  // Now get the stop date-string
  int next_pos = strProgramm.find(";", pos);
  strTmp = strProgramm.substr(pos+1, next_pos);
  
  year = atoi(strTmp.substr(0,4).c_str());
  month = atoi(strTmp.substr(4,2).c_str());
  day = atoi(strTmp.substr(6,2).c_str());
  hour = atoi(strTmp.substr(8,2).c_str());
  minute = atoi(strTmp.substr(10,2).c_str());
  seconds = atoi(strTmp.substr(12,2).c_str());
  
  tmpTime.tm_year = year - 1900;
  tmpTime.tm_mon = month - 1;
  tmpTime.tm_mday = day;
  tmpTime.tm_hour = hour+1;
  tmpTime.tm_min = minute;
  tmpTime.tm_sec = seconds;
  
  stop = mktime(&tmpTime);

  XBMC->Log(LOG_DEBUG, "Stop: %s Y:%d, M:%d, D:%d, H:%d, M:%d, S:%d (%s) time_t(%d)", __FUNCTION__, year, month, day, hour, minute, seconds, strTmp.c_str(), stop);
  
  // Get the channel id
  
  pos += next_pos;
  next_pos = strProgramm.find(";", pos);
  strTmp = strProgramm.substr(pos, next_pos-pos);

  //int iChannelId = atoi(strTmp.c_str());
  
  // Get the event id
  
  pos += (next_pos-pos)+2;
  strTmp = strProgramm.substr(pos+1, strProgramm.length()-pos);

  std::stringstream ss;
  ss << std::hex << strTmp;
  ss >> iEventID;
  
  return true;
}

void *N7Xml::Process()
{
  XBMC->Log(LOG_DEBUG, "%s - starting", __FUNCTION__);
  
  while(!IsStopped())
  {
    Sleep(1 * 1000);
    m_iUpdateTimer += 1;
    
    if ((int)m_iUpdateTimer > 10)
    {
      CLockObject lock(m_mutex);
      m_iUpdateTimer = 0;
      
      int iCurrentChannelId = GetEPGData();
      if (iCurrentChannelId != -1)
      {
        XBMC->Log(LOG_DEBUG, "%s - Trigger EPGUpdate for channel '%d'", __FUNCTION__, iCurrentChannelId);
        PVR->TriggerEpgUpdate(iCurrentChannelId);
      }
    }
    
  }
  
  CLockObject lock(m_mutex);
  m_started.Broadcast();
  
  return NULL;
}


int N7Xml::getChannelsAmount()
{ 
  return m_channels.size();
}

int N7Xml::GetUniqueChannelId(int iBackendChannelId)
{
  XBMC->Log(LOG_DEBUG, "%s Fetching UniqueChannelId for BackendChannelId '%d'", __FUNCTION__, iBackendChannelId);
  for (unsigned int i = 0;i<m_channels.size();  i++) 
  {
    if (iBackendChannelId == m_channels[i].iBackendChannelId)
    {
      XBMC->Log(LOG_DEBUG, "%s Found UniqueChannelId '%d' for BackendChannelId", __FUNCTION__, m_channels[i].iUniqueId);
      return m_channels[i].iUniqueId;
    }
  }
  return -1;
}

void N7Xml::list_channels()
{
  CLockObject lock(m_mutex);
  CStdString strUrl;
  strUrl.Format("http://%s:%i/n7channel_nt.xml", g_strHostname.c_str(), g_iPort);
  CStdString strXML;

  CCurlFile http;
  if(!http.Get(strUrl, strXML))
  {
    XBMC->Log(LOG_DEBUG, "%s - Could not open connection to N7 backend.", __FUNCTION__);
  }
  else
  {
    TiXmlDocument xml;
    xml.Parse(strXML.c_str());
    TiXmlElement* rootXmlNode = xml.RootElement();
    if (rootXmlNode == NULL)
      return;
    TiXmlElement* channelsNode = rootXmlNode->FirstChildElement("channel");
    if (channelsNode)
    {
      XBMC->Log(LOG_DEBUG, "%s - Connected to N7 backend.", __FUNCTION__);
      m_connected = true;
      int iUniqueChannelId = 0;
      TiXmlNode *pChannelNode = NULL;
      while ((pChannelNode = channelsNode->IterateChildren(pChannelNode)) != NULL)
      {
        CStdString strTmp;
        PVRChannel channel;

        /* unique ID */
        channel.iUniqueId = ++iUniqueChannelId;

        /* channel number */
        if (!XMLUtils::GetInt(pChannelNode, "number", channel.iBackendChannelId))
          continue;

        channel.iChannelNumber = channel.iUniqueId;

        /* channel name */
        if (!XMLUtils::GetString(pChannelNode, "title", strTmp))
          continue;
        channel.strChannelName = strTmp;

        /* icon path */
        const TiXmlElement* pElement = pChannelNode->FirstChildElement("media:thumbnail");
        channel.strIconPath = pElement->Attribute("url");

        /* channel url */
        if (!XMLUtils::GetString(pChannelNode, "guid", strTmp))
          channel.strStreamURL = "";
        else
          channel.strStreamURL = strTmp;

        XBMC->Log(LOG_DEBUG, "%s Adding channel: UniqueId '%d', BackendChannelId '%d', Name '%s'", __FUNCTION__, channel.iUniqueId, channel.iBackendChannelId, channel.strChannelName.c_str());

        m_channels.push_back(channel);
      }
    }
  }
}


PVR_ERROR N7Xml::requestChannelList(ADDON_HANDLE handle, bool bRadio)
{

  if (m_connected)
  {
    std::vector<PVRChannel>::const_iterator item;
    PVR_CHANNEL tag;
    for( item = m_channels.begin(); item != m_channels.end(); ++item)
    {
      const PVRChannel& channel = *item;
      memset(&tag, 0 , sizeof(tag));

      tag.iUniqueId       = channel.iUniqueId;
      tag.iChannelNumber  = channel.iChannelNumber;
      strncpy(tag.strChannelName, channel.strChannelName.c_str(), sizeof(tag.strChannelName) - 1);
      strncpy(tag.strStreamURL, channel.strStreamURL.c_str(), sizeof(tag.strStreamURL) - 1);
      strncpy(tag.strIconPath, channel.strIconPath.c_str(), sizeof(tag.strIconPath) - 1);

      XBMC->Log(LOG_DEBUG, "N7Xml - Loaded channel - %s.", tag.strChannelName);
      PVR->TransferChannelEntry(handle, &tag);
    }
  }
  else
  {
    XBMC->Log(LOG_DEBUG, "N7Xml - no channels loaded");
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR N7Xml::requestEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
{
  
  for (unsigned int iPtr = 0; iPtr < m_epg.size(); iPtr++)
  {
    EPGEntry entry = m_epg.at(iPtr);
    
    EPG_TAG broadcast;
    memset(&broadcast, 0, sizeof(EPG_TAG));
    
    broadcast.iUniqueBroadcastId  = entry.iEventId;
    broadcast.strTitle            = entry.strTitle.c_str();
    broadcast.iChannelNumber      = channel.iChannelNumber;
    broadcast.startTime           = entry.startTime;
    broadcast.endTime             = entry.endTime;
    broadcast.strPlotOutline      = ""; //unused
    broadcast.strPlot             = entry.strPlot.c_str();
    broadcast.strIconPath         = ""; // unused
    broadcast.iGenreType          = 0; // unused
    broadcast.iGenreSubType       = 0; // unused
    broadcast.strGenreDescription = "";
    broadcast.firstAired          = 0;  // unused
    broadcast.iParentalRating     = 0;  // unused
    broadcast.iStarRating         = 0;  // unused
    broadcast.bNotify             = false;
    broadcast.iSeriesNumber       = 0;  // unused
    broadcast.iEpisodeNumber      = 0;  // unused
    broadcast.iEpisodePartNumber  = 0;  // unused

    
    PVR->TransferEpgEntry(handle, &broadcast);
  }
  
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR N7Xml::getSignal(PVR_SIGNAL_STATUS &qualityinfo)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}


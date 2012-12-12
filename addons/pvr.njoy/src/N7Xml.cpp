

#include "N7Xml.h"
#include "tinyxml/tinyxml.h"
#include "tinyxml/XMLUtils.h"

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

N7Xml::N7Xml(void) :
        m_connected(false)
{
  m_iUpdateTimer = 0;
  list_channels();

  CreateThread();
}

N7Xml::~N7Xml(void)
{
  if (IsRunning())
    StopThread();
  
  m_channels.clear();
}

bool N7Xml::replace(std::string& str, const std::string& from, const std::string& to)
{
  size_t start_pos = str.find(from);
  if(start_pos == std::string::npos)
    return false;
  str.replace(start_pos, from.length(), to);
  return true;
}

bool N7Xml::GetEPGData()
{
  m_epg.clear();

  CStdString strUrl;
  strUrl.Format("http://%s:%i/n7chepg.xml", g_strHostname.c_str(), g_iPort);
  CStdString strXML;
  
  CCurlFile http;
  if(!http.Get(strUrl, strXML))
  {
    XBMC->Log(LOG_DEBUG, "%s - Could not open connection to N7 backend.",__FUNCTION__);
    return false;
  }

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
  
  
  int channelid = atoi(pElem->Attribute("id"));
  
  
  
  XBMC->Log(LOG_DEBUG, "%s - Parsing EPG for channel:'%d'", __FUNCTION__, channelid);
 
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
    
    epg.iChannelNumber = channelid;
    if (!XMLUtils::GetString(pNode, "title", strTmp))
      continue;
    
    epg.strTitle = strTmp;
    
    if (!XMLUtils::GetString(pNode, "description", strTmp))
      continue;
    
    epg.strPlot = strTmp;
    
    if (!XMLUtils::GetString(pNode, "programme", strTmp))
      continue;
    
    strTmp = "<hello>world</hello>"; //\n<tmp " + strTmp +  ">empty</tmp>";
    
    replace(strTmp, "event id= ", "eventid=");
    replace(strTmp, "channel id", "channelid");
    
    
    XBMC->Log(LOG_DEBUG, "%s: Programme string: %s", __FUNCTION__, strTmp.c_str());
    
    
    XBMC->Log(LOG_DEBUG, "%s - EPG Title:'%s', ChannelNumber:'%d'", __FUNCTION__, epg.strTitle.c_str(), epg.iChannelNumber);

  }
  
  return true;
}

void *N7Xml::Process()
{
  XBMC->Log(LOG_DEBUG, "%s - starting", __FUNCTION__);
  
  while(!IsStopped())
  {
    Sleep(1 * 1000);
    m_iUpdateTimer += 1;
    
    if ((int)m_iUpdateTimer > 2)
    {
      m_iUpdateTimer = 0;
      
      if (GetEPGData())
      {
        XBMC->Log(LOG_DEBUG, "%s - Fetching EPGData!", __FUNCTION__);
      }
       // PVR->TriggerEPGUpdate(); // TODO DEFINE CORRECTLY
      
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

void N7Xml::list_channels()
{
  CStdString strUrl;
  strUrl.Format("http://%s:%i/n7channel_nt.xml", g_strHostname.c_str(), g_iPort);
  CStdString strXML;

  CCurlFile http;
  if(!http.Get(strUrl, strXML))
  {
    XBMC->Log(LOG_DEBUG, "N7Xml - Could not open connection to N7 backend.");
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
      XBMC->Log(LOG_DEBUG, "N7Xml - Connected to N7 backend.");
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
        if (!XMLUtils::GetInt(pChannelNode, "number", channel.iChannelNumber))
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
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR N7Xml::getSignal(PVR_SIGNAL_STATUS &qualityinfo)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}


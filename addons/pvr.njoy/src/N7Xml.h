#pragma once

#include "client.h"
#include "platform/threads/threads.h"
#include <vector>

struct PVRChannel
{
  int         iUniqueId;
  int         iChannelNumber;
  int         iBackendChannelId;

  std::string strChannelName;
  std::string strIconPath;
  std::string strStreamURL;
  
  time_t updateTime;
  
  PVRChannel() 
  {
    iUniqueId      = 0;
    iChannelNumber = 0;
    iBackendChannelId = 0;
    strChannelName = "";
    strIconPath    = "";
    strStreamURL   = "";
    updateTime = 0;
  }
};

struct EPGEntry
{
    int iEventId;
    std::string strTitle;
    int iChannelNumber;
    time_t startTime;
    time_t endTime;
    std::string strPlot;
};


class CCurlFile
{
public:
  CCurlFile(void) {};
  ~CCurlFile(void) {};

  bool Get(const std::string &strURL, std::string &strResult);
};

class N7Xml : public PLATFORM::CThread
{
public:
  N7Xml(void);
  ~N7Xml(void);
  int getChannelsAmount(void);
  PVR_ERROR requestChannelList(ADDON_HANDLE handle, bool bRadio);
  PVR_ERROR requestEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd);
  PVR_ERROR getSignal(PVR_SIGNAL_STATUS &qualityinfo);
  void list_channels(void);

protected:
  virtual void *Process(void);
    
private:
  std::vector<EPGEntry> m_epg;
  std::vector<PVRChannel> m_channels;
  bool                    m_connected;
  PLATFORM::CMutex m_mutex;
  PLATFORM::CCondition<bool> m_started;
  int m_iUpdateTimer;
  int GetEPGData();
  
  void replace(std::string& str, const std::string& from, const std::string& to);
  bool GetInfoFromProgrammeString(const std::string& strProgramm, time_t& start, time_t& stop, unsigned int& iEventID);
  int GetUniqueChannelId(int iBackendChannelId);
};

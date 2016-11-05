#include "DetailState.h"
#include "comm_fun.h"
#include "GetSeriesMapIdList.h"
#include "PlaylistSppAction.h"
#include "UnionDataStoreMap.h"
#include "GetRecommendFromService.h"
#include "GetTimeLineAction.h"

int CDetailState::HandleEncode(CAsyncFrame *pFrame,
        CActionSet *pActionSet,
        CMsgBase *pMsg)
{
    const static int iActionStartID = 1000;
    const static int iUnionInstance = 10;
    static CGetSeriesMapIdList  cGetSeriesMapIdList;
    static CPlaylistAction      getPlaylistAction;
    static UnionDataStoreMapAction detailUnionAction;
    static CGetRecommendFromService getRecommendFromService;
    static CGetTimeLineAction getTimeLineAction;
    CMsg *msg = (CMsg*) pMsg;
    int iRet = 0;
    if (msg->m_iCmd != CMD_DETAIL_INFO)
    {
        pFrame->FRAME_LOG(LOG_ERROR, "Un Matched CMD");
        return -1;
    }
    msg->vctUnionData.clear();

    // 1、获取系列id
    if (msg->m_detailInfo.isNeedModType("1"))
    {
        g_monitor.addCnt(pFrame, "v_detail_series_cnt");
        ssdasn::TKeyNode oneNode;
        oneNode.key = TMEM_KEY_CLUSTER_ID_MAP_LIST + msg->m_detailInfo.m_strId;
        oneNode.retcode = -1;
        msg->m_vctTmemClusterIdMapNodes.push_back(oneNode);
    }

    // 2、获取预告片花絮部分
    if (msg->m_detailInfo.isNeedModType("2"))
    {
        buildPlaylistConfig(pFrame, msg);
        g_monitor.addCnt(pFrame, "v_detail_huaxu_cnt");
    }

    // 3、剧照  || 增加小视频，一起构成一个Model
    if (msg->m_detailInfo.isNeedModType("3"))
    {
        {
            g_monitor.addCnt(pFrame, "v_detail_still_cnt");
            int iTypeId = 2003;
            if (msg->m_detailInfo.m_strDataType == "3")
            {
                iTypeId = 2005;
            }
            string strTmp;
            addOneUnionReq(pFrame,
                    iTypeId,
                    g_mapUnionDetailFields,
                    g_mapUnionFields2DetailOutputFields,
                    msg->m_detailInfo.m_strId,
                    iUnionInstance,
                    msg->vctUnionData,
                    strTmp);
        }

      //  当需要剧照的时候就添加小视频的数据
        {
            string strName = "TIME_LINE_ACTION";
            int iActionID = iActionStartID - 1;
            if( 0 != addOneAction(pFrame, pActionSet, strName, g_mapStorageInfo, getTimeLineAction,iActionID) )
            {
                pFrame->FRAME_LOG(LOG_ERROR, "Add Action Failed iActionID[%d],iRet[%d]", iActionID, iRet);
            }
        }
    }

    // 4、相关推荐
    if (msg->m_detailInfo.isNeedModType("4"))
    {
        if (msg->m_detailInfo.m_strDataType == "2")// || msg->m_detailInfo.m_strDataType == "3")
        {
            g_monitor.addCnt(pFrame, "v_detail_rec_cnt");
            string strName = "DETAIL_RECOMMEND_ACTION";
            int iActionID = iActionStartID ;
            if (0 != addOneAction(pFrame, pActionSet, strName, g_mapStorageInfo, getRecommendFromService, iActionID))
            {
                pFrame->FRAME_LOG(LOG_ERROR, "Add Action Failed:[%d]", iRet);
            }
        }
    }

    if (msg->m_vctTmemClusterIdMapNodes.size() > 0)
    {
        string strName = "TMEM_CLUSTER_ID_LIST";
        int iActionId = iActionStartID + 1;
        if (0 != addOneAction(pFrame, pActionSet, strName, g_mapStorageInfo, cGetSeriesMapIdList, iActionId))
        {
            pFrame->FRAME_LOG(tbase::tlog::LOG_DEBUG, "failed to add %s action:[%d]", strName.c_str(), iActionId);
        }
    }

    if (!msg->m_vctPlaylistConf.empty())
    {
        string strName = "PLAYLIST_SERVICE";
        int iActionId = iActionStartID + 2;
        if (0 != addOneAction(pFrame, pActionSet, strName, g_mapStorageInfo, getPlaylistAction, iActionId))
        {
            pFrame->FRAME_LOG(tbase::tlog::LOG_DEBUG, "failed to add %s action:[%d]", strName.c_str(), iActionId);
        }
    
    }

    if (!msg->vctUnionData.empty())
    {
        string strName = "UNION_DETAIL";
        for (size_t i = 0; i < msg->vctUnionData.size(); i++)
        {
            int iActionId = iActionStartID + 5 + i;
            if (0 != addOneAction(pFrame, pActionSet, strName, g_mapStorageInfo, detailUnionAction, iActionId))
            {
                pFrame->FRAME_LOG(tbase::tlog::LOG_DEBUG, "failed to add %s action:[%d]", strName.c_str(), iActionId);
                iRet = -21;
            }
        }

    }
    gettimeofday(&msg->m_tvs, NULL);
    return iRet;
}

int CDetailState::HandleProcess(CAsyncFrame *pFrame,
        CActionSet *pActionSet,
        CMsgBase *pMsg)
{
    CMsg *msg = (CMsg*) pMsg;
    int iNextState = STATE_DETAIL_END;
    if (!msg->m_detailInfo.m_strClusterId.empty())
    {
        CRequestParam &searchReq = msg->m_SearchReq;
        searchReq.m_iRequestId = 1;
        searchReq.m_strQuery = msg->m_detailInfo.m_strClusterId;
        searchReq.m_strClusterId = msg->m_detailInfo.m_strClusterId;
        searchReq.m_strSType = "120";
        searchReq.decodeRequest(pFrame);
    }

    bool bPermit = true;
    if (!msg->vctUnionData.empty())
    {
        decodeStillUnionInfo(pFrame, msg);
        checkPermission(pFrame, msg, bPermit);
    }

    if (!msg->m_vctPlaylistConf.empty() && bPermit)
    {
        decodePlaylistInfo(pFrame, msg);
    }

    return iNextState;
}

int CDetailState::buildPlaylistConfig(CAsyncFrame *pFrame, CMsg *msg)
{
    int iRet = 0;
    if (msg->m_detailReq.id.empty())
    {
        pFrame->FRAME_LOG(LOG_DEBUG, "Ignore One Detail Request");
        return -1001;
    }
    CPlaylistReq plReq;
    plReq.iReqType = 10;
    string strConfig;
    strConfig = "platform:2";
    if (msg->m_detailInfo.m_vctNeedModType.empty())
    {
        strConfig.append(";need_long_or_short_video:all");
        strConfig.append(";need_which_website_play_url:first");
        strConfig.append(";trailer_start:1");
        strConfig.append(";trailer_end:");
        strConfig.append(util::itoa(DETAIL_TRAILER_PAGE_SIZE));
    }
    else
    {
        strConfig.append(";need_long_or_short_video:short");
        strConfig.append(";need_which_website_play_url:qq");
    }

    map<string, string> &mapConfig = plReq.mapConfig;
    mapConfig[msg->m_detailReq.id] = strConfig;

    plReq.strReqId = REQ_ID_FOR_DETAIL_PLAYLIST;
    msg->m_vctPlaylistConf.push_back(plReq);
    return iRet;
}
int CDetailState::decodePlaylistInfo(CAsyncFrame *pFrame, CMsg *msg)
{
    int iRet = 0;
    if (msg->m_playlistResponse.mapResponseBody.find("playlist") == msg->m_playlistResponse.mapResponseBody.end())
    {
        pFrame->FRAME_LOG(LOG_DEBUG, "Not Found Playlist response");
        return 0;
    }
    BaseResult &respBody = msg->m_playlistResponse.mapResponseBody.find("playlist")->second;
    if (respBody.mapDoctID2RefResults.find(msg->m_detailReq.id) == respBody.mapDoctID2RefResults.end())
    {
        pFrame->FRAME_LOG(LOG_DEBUG, "NO Found id:[%s] playlist", msg->m_detailReq.id.c_str());
        return 0;
    }

    BaseResult &result = respBody.mapDoctID2RefResults.find(msg->m_detailReq.id)->second;

    if (!result.vctDocID.empty() && result.vctDocID[0] == "qq")
    {
        int iPlType = 0;
        CProtocolHelper::getAttributeInt(result, "qq", "video_type", iPlType);
        if (iPlType == 1)
        {
            msg->m_detailInfo.m_bQQNormal = true;
            pFrame->FRAME_LOG(LOG_DEBUG, "Have Normal QQ playlist:[%s]", msg->m_detailReq.id.c_str());
        }
    
    }

    if (result.mapDoctID2RefResults.find("qq_trailer") != result.mapDoctID2RefResults.end())
    {
        BaseResult &playlist = result.mapDoctID2RefResults.find("qq_trailer")->second;
        PosterListMod &mod = msg->m_detailInfo.m_prevListMod;
        CProtocolHelper::getAttributeInt(result, "qq_trailer", "total_episode", mod.totalNum);
        mod.needAsync = false;
        if (mod.totalNum > DETAIL_TRAILER_PAGE_SIZE && msg->m_detailInfo.m_vctNeedModType.empty())
        {
            mod.needAsync = true;
        }
        mod.uiType = 1;
        mod.maxRow = 1;
        if (msg->m_detailInfo.m_vctNeedModType.empty())
        {
            mod.modTitle = "预告和花絮";
        }
        else
        {
            mod.modTitle = "预告片";
        }

        PosterListMod &huaxuMod = msg->m_detailInfo.m_huaxuListMod;
        huaxuMod.needAsync = false;
        huaxuMod.uiType = 1;
        huaxuMod.maxRow = 1;
        huaxuMod.modTitle = "花絮";
        Json::Value value;
        Json::FastWriter writer;
        value["id"] = msg->m_detailReq.id;
        mod.asyncParam = writer.write(value);
        for (size_t i = 0; i < playlist.vctDocID.size(); i ++)
        {
            const string &strId = playlist.vctDocID[i];
            Poster poster;
            poster.id = strId;
            poster.uiType = 1;
            const string &strDataType = msg->m_detailInfo.m_strDataType;
            if (strDataType == "3")
            {
                poster.dataType = 2;
            }
            else
            {
                poster.dataType = 1;
            }
            CProtocolHelper::getAttributeString(playlist, strId, "title", poster.title);
            CProtocolHelper::getAttributeString(playlist, strId, "pic", poster.pic);
            CProtocolHelper::getAttributeString(playlist, strId, "url", poster.url);
            int iType = 0;
            CProtocolHelper::getAttributeInt(playlist, strId, "type", iType);
            if (iType == 2)
            {
                pushMarkLabelMarkTrailer(pFrame, msg, poster.markLabelList);
            }
            if (msg->m_detailInfo.m_vctNeedModType.empty())
            {
                mod.posterList.push_back(poster);
            }
            else
            {
                if (iType == 2)
                {
                    mod.posterList.push_back(poster);
                }
                else
                {
                    huaxuMod.posterList.push_back(poster);
                }
            }
            pFrame->FRAME_LOG(LOG_DEBUG, "Push one prevlist item:[%s] [%s]", strId.c_str(), poster.title.c_str());

        }
    }


    return iRet;
}

int CDetailState::checkPermission(CAsyncFrame *pFrame, CMsg *msg, bool &bPermit)
{
    int iRet = 0;
    const string &strId = msg->m_detailInfo.m_strId;
    if (msg->mapUnionResults.find(strId) == msg->mapUnionResults.end())
    {
        bPermit = false;
        pFrame->FRAME_LOG(LOG_DEBUG, "ignore one id as no union:[%s]", strId.c_str());
        return 0;
    }
    map<string, string> &mapData = msg->mapUnionResults.find(strId)->second;
    string strCoverStatus, strDataStatus, strHideFlag, strSearchFlag, strPlayright;

    map<string, string>::iterator mapIt = mapData.find("type");
    if (mapIt != mapData.end())
    {
        msg->m_detailInfo.m_iVideoType = atoi(mapIt->second.c_str());
    }
    mapIt = mapData.find("playright");
    if (mapIt != mapData.end())
    {
        strPlayright = mapIt->second;
    }
    if (msg->m_detailInfo.m_strDataType == "3")
    {
        mapIt = mapData.find("hide_flag");
        if (mapIt != mapData.end())
        {
            strHideFlag = mapIt->second;
        }
        mapIt = mapData.find("data_checkup_grade");
        if (mapIt != mapData.end())
        {
            strSearchFlag = mapIt->second;
        }

        if (strHideFlag != "0" && strSearchFlag != "0")
        {
            pFrame->FRAME_LOG(LOG_DEBUG, "Ignore one request as not up");
            bPermit = false;
            return 0;
        }
    }
    else
    {
        mapIt = mapData.find("cover_checkup_grade");
        if (mapIt != mapData.end())
        {
            strCoverStatus = mapIt->second;
        }
        mapIt = mapData.find("data_checkup_grade");
        if (mapIt != mapData.end())
        {
            strDataStatus = mapIt->second;
        }
        if (strCoverStatus != "4" && strDataStatus != "4")
        {
            pFrame->FRAME_LOG(LOG_DEBUG, "Ignore one request as not up");
            bPermit = false;
            return 0;
        }
    }

    if (strPlayright.find(util::itoa(msg->m_iPlatform)) == string::npos)
    {
        pFrame->FRAME_LOG(LOG_DEBUG, "Ignore one request as have no playright");
        bPermit = false;
        return 0;
    }
    return iRet;
}

int CDetailState::decodeStillUnionInfo(CAsyncFrame *pFrame, CMsg *msg)
{
    int iRet = 0;
    pFrame->FRAME_LOG(LOG_TRACE, "gona decodeStillUnionInfo ");

    int iType = 2003;
    if (msg->m_detailInfo.m_strDataType == "3")
    {
        iType = 2005;
    }
    if (g_mapUnionFields2DetailOutputFields.find(iType) == g_mapUnionFields2DetailOutputFields.end())
    {
        pFrame->FRAME_LOG(LOG_DEBUG, "Error Not Found type for union:[%d]", iType);
        return 0;
    }
    map<string, string> &mapField = g_mapUnionFields2DetailOutputFields.find(iType)->second;
    const string &strId = msg->m_detailInfo.m_strId;
    if (msg->mapUnionResults.find(strId) == msg->mapUnionResults.end())
    {
        pFrame->FRAME_LOG(LOG_DEBUG, "ignore one id as no union:[%s]", strId.c_str());
        return 0;
    }
    map<string, string> &mapData = msg->mapUnionResults.find(strId)->second;
    for (map<string, string>::iterator mapIt = mapData.begin(); mapIt != mapData.end(); mapIt ++)
    {
        if (mapField.find(mapIt->first) == mapField.end())
        {
            pFrame->FRAME_LOG(LOG_DEBUG, "Ignore One Field as no out put:[%s]", mapIt->first.c_str());
            continue;
        }
        const string &strOutputField = mapField.find(mapIt->first)->second;
        if (strOutputField == "still_list")
        {
            vector<string> vctPicUrls;
            util::split(mapIt->second, "+", vctPicUrls);

            pFrame->FRAME_LOG(LOG_TRACE, "gona build detail time line and picture,TimeLine size[%d], Pictures size[%d]",msg->m_vctTimeLine.size(), vctPicUrls.size());
            PosterListMod &mod = msg->m_detailInfo.m_tinyVideoMod;
            if (msg->m_detailInfo.m_vctNeedModType.empty())//中间页的条件
            {
                if(vctPicUrls.size() == 0)
                {
                    mod.modTitle = "小视频";
                }
                else if(msg->m_vctTimeLine.size() == 0)
                {
                    mod.modTitle = "剧照";
                }
                else
                {
                    mod.modTitle = "小视频和剧照";
                }
            }
            /*
             *  构造【剧照和小视频】模块 ---5/行
             *  1、展示最多三行，（目前限制总个数是 15）
             *  2、小视频最多两行（10）
             *  3、剧照最少一行（5）
             *  4、当总的个数大于 15 时，给出查看更多的入口
             * */ 
            mod.maxRow = 1;
            mod.totalNum = vctPicUrls.size() + msg->m_vctTimeLine.size();
            mod.uiType = 1;
            mod.needAsync = false;
            mod.nextPageNum = msg->m_iTimeLinePageNumber; // 在TimeLine接口返回的iNextPage更新到该字段
            pFrame->FRAME_LOG(LOG_TRACE, "mod.nextPageNum  <== msg->m_iTimeLinePageNumber [%d]", msg->m_iTimeLinePageNumber);
            if (mod.totalNum > DETAIL_STILL_PAGE_SIZE)
            {
                mod.needAsync = true;
            }
            /*填充小视频*/
            for (size_t j = 0; j < msg->m_vctTimeLine.size(); j++ )
            {
                Poster poster;
                poster.id = msg->m_vctTimeLine[j].vid;
                poster.pic = msg->m_vctTimeLine[j].imgurl;
                poster.dataType = 1001;
                poster.uiType = 1;
                mod.posterList.push_back(poster);
                if (msg->m_detailInfo.m_vctNeedModType.empty())
                {
                    if (mod.posterList.size() >= 10)
                    {
                        pFrame->FRAME_LOG(LOG_TRACE, "fill the TimeLine, reach size[%d]", mod.posterList.size());
                        break;
                    }
                }
            }
            /*填充剧照*/
            pFrame->FRAME_LOG(LOG_TRACE, "[剧照 : Pictures]0 == msg->m_detailInfo.m_iNextPageNumber [%d]", msg->m_detailInfo.m_iNextPageNumber);
            if(msg->m_detailInfo.m_iNextPageNumber == 0 )  // 这样保持剧照的状态不会随着异步拉取而改变
            {
                for (size_t i = 0; i < vctPicUrls.size(); i ++)
                {
                    Poster poster;
                    poster.id = util::utoa(i);
                    poster.pic = vctPicUrls[i];
                    poster.dataType = 1000;
                    poster.uiType = 1;
                    mod.posterList.push_back(poster);
                    if (msg->m_detailInfo.m_vctNeedModType.empty())
                    {
                        if (mod.posterList.size() >= DETAIL_STILL_PAGE_SIZE)
                        {
                            pFrame->FRAME_LOG(LOG_TRACE, "fill the TimeLine & Picture, reach size[%d]", mod.posterList.size());
                            break;
                        }
                    }
                }
            }

        }
    }

    return iRet;
}

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "client.h"

namespace qqmusic::api {

    struct QrCodeSession {
        std::string qrsig;
        std::vector<uint8_t> png_bytes;
        int status_code = 66; // 66: waiting, 67: scanned, 65: expired, 0: success
        std::string status_msg;
    };

    struct UserSession {
        bool logged_in = false;
        std::string uin;
        std::string nickname;
        std::string musickey;
        std::string musicid;
    };

    void Init();
    UserSession GetUserSession();
    bool LoadCredential();
    bool SaveCredential(const std::string &uin, const std::string &musickey, const std::string &musicid, const std::string &nickname);
    void Logout();

    bool RequestQrCode(QrCodeSession &out_session);
    bool PollQrCode(QrCodeSession &session);

    bool SearchSongs(const std::string &query, int page, int page_size, std::vector<QqMusicOnlineSong> &out_songs, int &out_total);
    bool GetTopChart(int top_id, int offset, int count, std::vector<QqMusicOnlineSong> &out_songs);
    bool GetMyFavorites(int offset, int count, std::vector<QqMusicOnlineSong> &out_songs, int &out_total);
    // 个人雷达推荐：一次请求批量拉取 pages 个雷达页（每页约 10 首）。
    bool GetRadarSongs(int start_page, int pages, std::vector<QqMusicOnlineSong> &out_songs);
    bool GetGuessRecommend(int start_page, int pages, std::vector<QqMusicOnlineSong> &out_songs);

    // 在线集合（歌手 / 专辑 / 歌单）：kind 1=歌手 2=专辑 3=歌单
    bool SearchCollections(const std::string &query, int kind, int page, int page_size,
                           std::vector<QqMusicOnlineCollection> &out_items, int &out_total);
    // 收藏的专辑 / 歌单
    bool GetFavAlbums(int offset, int count, std::vector<QqMusicOnlineCollection> &out_items, int &out_total);
    bool GetFavPlaylists(int offset, int count, std::vector<QqMusicOnlineCollection> &out_items, int &out_total);
    // 集合下的曲目：专辑/歌手按 mid，歌单按 disstid
    bool GetAlbumSongs(const std::string &albummid, int begin, int num, std::vector<QqMusicOnlineSong> &out_songs, int &out_total);
    bool GetSingerSongs(const std::string &singermid, int begin, int num, std::vector<QqMusicOnlineSong> &out_songs, int &out_total);
    bool GetPlaylistSongs(u64 disstid, int begin, int num, std::vector<QqMusicOnlineSong> &out_songs, int &out_total);

    std::string GetSongPlayUrl(const std::string &songmid, const std::string &media_mid);
    // 歌曲所属专辑 MID（用于封面兜底：老队列/搜索结果的 qqm:// 路径可能缺 album_mid）。
    std::string GetSongAlbumMid(const std::string &songmid);
    // 获取歌曲标准 LRC 歌词文本
    bool GetSongLyric(const std::string &songmid, std::string &out_lrc);

} // namespace qqmusic::api

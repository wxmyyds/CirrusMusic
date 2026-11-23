#pragma once

#include "fake_http.h"

#include <QByteArray>
#include <QList>

// Synthetic Bilibili answers only: the public JSON each stage returns, in the order the API
// asks. No real endpoint, signature or CDN address is involved.
namespace cirrus::test
{

class FakeGetManager : public QNetworkAccessManager
{
  public:
    QList<Plan> plans;
    QList<QNetworkRequest> requests;

  protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request,
                                 QIODevice*) override
    {
        Q_ASSERT(operation == GetOperation);
        requests.append(request);
        return new FakeReply(request, plans.isEmpty() ? Plan{} : plans.takeFirst(), this);
    }
};

inline const QByteArray kBiliFingerprint =
    R"({"code":0,"data":{"buvid3":"BV3-ANON","buvid4":"BV4-ANON"}})";
inline const QByteArray kBiliNav = R"({"code":0,"data":{"wbi_img":{
    "img_url":"https://i0.hdslb.com/bfs/wbi/7cd084941338484aae1ad9425b84077c.png",
    "sub_url":"https://i0.hdslb.com/bfs/wbi/4932caff0ff746eab6f01bf08b70ac45.png"}}})";
inline const QByteArray kBiliSearch = R"({"code":0,"data":{"result":[
    {"type":"video","aid":123,"bvid":"BV1testpage","title":"Song","author":"Artist",
     "duration":"03:20","play":1000}]}})";
inline const QByteArray kBiliView = R"({"code":0,"data":{"aid":123,"bvid":"BV1testpage",
    "title":"Song","owner":{"name":"Artist"},"duration":200,
    "pages":[{"cid":456,"page":1,"part":"Song","duration":200}]}})";
inline const QByteArray kBiliPlayUrlSized = R"({"code":0,"data":{"timelength":200000,"dash":{
    "video":[{"id":80,"baseUrl":"https://v.bilivideo.com/v"}],
    "audio":[{"id":30280,"baseUrl":"https://cn.bilivideo.com/a.mp4","bandwidth":192000,
              "mimeType":"audio/mp4","size":4800000}]}}})";
inline const QByteArray kBiliPlayUrlUnsized = R"({"code":0,"data":{"timelength":200000,"dash":{
    "video":[{"id":80,"baseUrl":"https://v.bilivideo.com/v"}],
    "audio":[{"id":30280,"baseUrl":"https://cn.bilivideo.com/a.mp4","bandwidth":192000,
              "mimeType":"audio/mp4"}]}}})";

inline Plan biliJson(const QByteArray& body)
{
    Plan plan;
    plan.body = body;
    plan.status = 200;
    return plan;
}

// The five answers a successful resolve consumes when the playurl carries the byte length.
inline QList<Plan> biliResolveSized()
{
    return {biliJson(kBiliFingerprint), biliJson(kBiliNav), biliJson(kBiliSearch),
            biliJson(kBiliView), biliJson(kBiliPlayUrlSized)};
}

} // namespace cirrus::test

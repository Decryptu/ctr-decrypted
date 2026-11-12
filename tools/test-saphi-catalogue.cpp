// g++ -std=c++17 -Wall -Wextra -Werror -I include -I ap/vendor/json/include
// tools/test-saphi-catalogue.cpp platform/native_saphi_catalogue.cpp -o /tmp/test-saphi-catalogue
#include <platform/native_saphi_catalogue.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
using json = nlohmann::json;
static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while(0)
static json fixture()
{
    json lev={{"id",11},{"type","lev"},{"version","1.0.0"},{"modes",{"arcade"}},
        {"file_size",4096},{"crc32_hash",4294967295u},{"download_url","/api/v3/tracks/7/downloads/11"},{"is_current",true}};
    json vrm=lev;vrm["id"]=12;vrm["type"]="vrm";vrm["download_url"]="/api/v3/tracks/7/downloads/12";
    return {{"data",json::array({{{"id",7},{"name","Fixture Track"},{"author","Creator"},{"type","race"},
        {"is_active",true},{"lap_count",3},{"downloads",{lev,vrm}}}})}};
}
static json decrypted_index()
{
    json file={{"bytes",1000},{"crc32",5}};
    return {{"schema_version",1},{"tracks",json::array({{{"id","c2a1f7f0-7a6e-5b43-9d3a-2f1e8b6a4d10"},
        {"title","Own Track"},{"author","Decrypt"},{"version","1.0.0"},{"folder","own-track-c2a1f7f0"},
        {"pin",std::string(64,'a')},{"manifest_bytes",700},{"lev",file},{"vrm",file},{"modes",{"arcade","time_trial"}}}})}};
}
static int add(CustomSaphiCatalogue *c, const json &j)
{
    char error[256];auto bytes=j.dump();
    return CustomSaphi_AddDecrypted(c,bytes.data(),bytes.size(),error,sizeof error);
}
static int parse(const json &j, CustomSaphiCatalogue **out)
{
    char error[256];auto bytes=j.dump();
    return CustomSaphi_ParseCatalogue(bytes.data(),bytes.size(),out,error,sizeof error);
}
int main(int argc,char **argv)
{
    CustomSaphiCatalogue *c=nullptr;
    if(argc==2)
    {
        std::ifstream in(argv[1]);std::string s((std::istreambuf_iterator<char>(in)),{});char error[256];
        if(!CustomSaphi_ParseCatalogue(s.data(),s.size(),&c,error,sizeof error)) { std::puts(error);return 1; }
        size_t current=0, unavailable=0;
        for(size_t i=0;i<CustomSaphi_Count(c);i++) {auto *r=CustomSaphi_Row(c,i);current+=r->current;unavailable+=!!r->disabledReason[0];}
        std::printf("Live catalogue: %zu revisions, %zu current, %zu unavailable pairs\n",CustomSaphi_Count(c),current,unavailable);
        CustomSaphi_Free(&c);return 0;
    }
    /* The Decrypted catalogue: a track of its own is a row of its own. */
    auto j=fixture();auto own=decrypted_index();CHECK(parse(j,&c));CHECK(add(c,own));CHECK(CustomSaphi_Count(c)==2);
    {
        auto *row=CustomSaphi_Row(c,1);auto *fixtureRow=CustomSaphi_Row(c,0);
        CHECK(!strcmp(row->title,"Own Track") && row->decrypted && row->trackID<0 && row->current && row->modeTags==3);
        CHECK(!strcmp(row->pin,std::string(64,'a').c_str()) && row->manifestBytes==700 && !strcmp(row->folder,"own-track-c2a1f7f0"));
        CHECK(!fixtureRow->decrypted && !fixtureRow->packageUuid[0]);
    }
    CustomSaphi_Free(&c);
    /* Its package's picture is listed: fetched from its folder in the catalogue.
       A picture of another size is refused. */
    own["tracks"][0]["picture"]={{"bytes",9408},{"crc32",77}};
    CHECK(parse(j,&c));CHECK(add(c,own));
    {
        auto *row=CustomSaphi_Row(c,1);
        CHECK(row->picture.id && row->picture.bytes==9408 && row->picture.crc32==77 &&
              !strcmp(row->picture.path,"tracks/own-track-c2a1f7f0/metadata/picture.ctp"));
    }
    CustomSaphi_Free(&c);
    own["tracks"][0]["picture"]["bytes"]=9000;CHECK(parse(j,&c));CHECK(!add(c,own));CustomSaphi_Free(&c);
    own["tracks"][0].erase("picture");
    /* Saphi lists it by id, same version: one row, Saphi's, knowing the UUID. */
    own["tracks"][0]["saphi_track_id"]=7;own["tracks"][0]["title"]="Fixture Track";
    CHECK(parse(j,&c));CHECK(add(c,own));CHECK(CustomSaphi_Count(c)==1);
    CHECK(!CustomSaphi_Row(c,0)->decrypted && !strcmp(CustomSaphi_Row(c,0)->packageUuid,"c2a1f7f0-7a6e-5b43-9d3a-2f1e8b6a4d10"));CustomSaphi_Free(&c);
    /* Ours is newer: both rows, one track, so it is a version of Saphi's. */
    own["tracks"][0]["version"]="1.1.0";
    CHECK(parse(j,&c));CHECK(add(c,own));CHECK(CustomSaphi_Count(c)==2);
    CHECK(CustomSaphi_Row(c,0)->trackID==7 && CustomSaphi_Row(c,1)->trackID==7);
    CHECK(CustomSaphi_Row(c,0)->decrypted!=CustomSaphi_Row(c,1)->decrypted);CustomSaphi_Free(&c);
    /* No id, but Saphi has the same LEV and VRM: the same track. */
    own=decrypted_index();own["tracks"][0]["version"]="1.0.0";
    own["tracks"][0]["lev"]={{"bytes",4096},{"crc32",4294967295u}};own["tracks"][0]["vrm"]=own["tracks"][0]["lev"];
    CHECK(parse(j,&c));CHECK(add(c,own));CHECK(CustomSaphi_Count(c)==1);CHECK(CustomSaphi_Row(c,0)->packageUuid[0]);CustomSaphi_Free(&c);
    /* Paired identical files carry hashes through to the surviving Saphi row. */
    own["tracks"][0]["lev"]["sha256"]=std::string(64,'a');
    own["tracks"][0]["vrm"]["sha256"]=std::string(64,'b');
    CHECK(parse(j,&c));CHECK(add(c,own));
    CHECK(!strcmp(CustomSaphi_Row(c,0)->lev.sha256,std::string(64,'a').c_str()));
    CHECK(!strcmp(CustomSaphi_Row(c,0)->vrm.sha256,std::string(64,'b').c_str()));CustomSaphi_Free(&c);
    own["tracks"][0]["lev"]["sha256"]=std::string(64,'-');
    CHECK(parse(j,&c));CHECK(!add(c,own));CHECK(CustomSaphi_Count(c)==1);CustomSaphi_Free(&c);
    /* A Saphi row that cannot be downloaded does not hide ours. */
    {
        auto gone=j;gone["data"][0]["downloads"][0]["file_size"]=0;gone["data"][0]["downloads"][0]["download_url"]=nullptr;
        auto linked=decrypted_index();linked["tracks"][0]["saphi_track_id"]=7;
        CHECK(parse(gone,&c));CHECK(add(c,linked));CHECK(CustomSaphi_Count(c)==2);CustomSaphi_Free(&c);
    }
    /* Refusals leave the catalogue as it was. */
    CHECK(parse(j,&c));
    for(auto edit:std::initializer_list<std::pair<const char *,json>>{{"folder","../x"},{"folder","Own"},{"id","nope"},{"pin","abc"},{"manifest_bytes",0}})
    {
        auto bad=decrypted_index();bad["tracks"][0][edit.first]=edit.second;
        CHECK(!add(c,bad));CHECK(CustomSaphi_Count(c)==1);
    }
    {
        auto twice=decrypted_index();twice["tracks"].push_back(twice["tracks"][0]);
        CHECK(!add(c,twice));CHECK(CustomSaphi_Count(c)==1);
        auto future=decrypted_index();future["schema_version"]=2;CHECK(!add(c,future));
    }
    CustomSaphi_Free(&c);
    CHECK(parse(j,&c));CHECK(CustomSaphi_Count(c)==1);
    auto r=*CustomSaphi_Row(c,0);CHECK(r.trackID==7 && r.lev.id==11 && r.vrm.id==12);
    CHECK(r.lev.crc32==UINT32_MAX && r.sourceLaps==3 && r.current && !r.disabledReason[0]);
    CHECK(!CustomSaphi_Row(c,1));CHECK(!parse(j,&c));CHECK(CustomSaphi_Count(c)==1);CustomSaphi_Free(&c);
    j["data"][0]["downloads"][1]["modes"]={"time_trial"};
    CHECK(parse(j,&c));CHECK(CustomSaphi_Count(c)==2);
    CHECK(CustomSaphi_Row(c,0)->disabledReason[0] && CustomSaphi_Row(c,1)->disabledReason[0]);CustomSaphi_Free(&c);
    /* A mode tag this client does not know (Saphi added ring_rally) is left
       out of the bits and never fails the catalogue; it still keeps pairs apart. */
    j=fixture();for(int i=0;i<2;i++) j["data"][0]["downloads"][i]["modes"]={"arcade","ring_rally"};
    CHECK(parse(j,&c));CHECK(CustomSaphi_Count(c)==1);
    CHECK(CustomSaphi_Row(c,0)->modeTags==1 && !CustomSaphi_Row(c,0)->disabledReason[0] && CustomSaphi_Row(c,0)->current);CustomSaphi_Free(&c);
    j["data"][0]["downloads"][1]["modes"]={"arcade"};
    CHECK(parse(j,&c));CHECK(CustomSaphi_Count(c)==2);
    CHECK(CustomSaphi_Row(c,0)->disabledReason[0] && CustomSaphi_Row(c,1)->disabledReason[0]);CustomSaphi_Free(&c);
    j=fixture();for(int i=0;i<2;i++) j["data"][0]["downloads"][i]["modes"]={"time_trial","relic_race","ctr_challenge","arcade","ring_rally","crystal_challenge","battle"};
    CHECK(parse(j,&c));CHECK(CustomSaphi_Count(c)==1 && CustomSaphi_Row(c,0)->modeTags==63);CustomSaphi_Free(&c);
    j=fixture();j["data"][0]["downloads"][0]["modes"]={"ring_rally","ring_rally"};CHECK(!parse(j,&c));
    j=fixture();j["data"][0]["downloads"][0]["modes"]={"arcade","arcade"};CHECK(!parse(j,&c));
    j=fixture();j["data"][0]["downloads"].push_back(j["data"][0]["downloads"][0]);CHECK(!parse(j,&c));CHECK(!c);
    j=fixture();j["data"].push_back(j["data"][0]);CHECK(!parse(j,&c));
    for(const char *url:{"https://evil.test/api/v3/tracks/7/downloads/11","/api/v3/tracks/8/downloads/11", "//www.projectsaphi.com/x"})
    {j=fixture();j["data"][0]["downloads"][0]["download_url"]=url;CHECK(!parse(j,&c));}
    j=fixture();j["data"][0]["downloads"][0]["download_url"]="https://www.projectsaphi.com/api/v3/tracks/7/downloads/11";
    CHECK(parse(j,&c));CustomSaphi_Free(&c);
    j=fixture();j["data"][0]["downloads"][0]["file_size"]=9*1024*1024;CHECK(!parse(j,&c));
    j=fixture();j["data"][0]["downloads"][0]["crc32_hash"]=-1;CHECK(!parse(j,&c));
    j=fixture();j["data"][0]["downloads"][0]["file_size"]=0;j["data"][0]["downloads"][0]["download_url"]=nullptr;
    CHECK(parse(j,&c));CHECK(CustomSaphi_Row(c,0)->disabledReason[0]);CHECK(!CustomSaphi_Row(c,0)->lev.path[0]);CustomSaphi_Free(&c);
    j=fixture();j["data"][0]["name"]="Bad\nTitle";CHECK(!parse(j,&c));
    j=fixture();j["data"][0]["is_active"]=false;CHECK(parse(j,&c));CHECK(!CustomSaphi_Count(c));CustomSaphi_Free(&c);
    /* The retail tracks Saphi lists in its "original" collection are left out. */
    j=fixture();j["data"][0]["collection"]="original";CHECK(parse(j,&c));CHECK(!CustomSaphi_Count(c));CustomSaphi_Free(&c);
    j=fixture();j["data"][0]["collection"]="community";CHECK(parse(j,&c));CHECK(CustomSaphi_Count(c)==1);CustomSaphi_Free(&c);
    /* Track audio: one current .sca goes to the current revision only; none,
       two current, an old one, or a bad row leaves it off without failing. */
    auto withSca=[](std::initializer_list<json> scas){
        auto j=fixture();auto old=j["data"][0]["downloads"][0];old["id"]=9;old["version"]="0.9.0";old["is_current"]=false;
        old["download_url"]="/api/v3/tracks/7/downloads/9";j["data"][0]["downloads"].push_back(old);
        auto oldv=old;oldv["id"]=10;oldv["type"]="vrm";oldv["download_url"]="/api/v3/tracks/7/downloads/10";j["data"][0]["downloads"].push_back(oldv);
        for(const auto &s:scas) j["data"][0]["downloads"].push_back(s);
        return j;};
    json sca={{"id",13},{"type","sca"},{"version","1.0.0"},{"modes",json::array()},{"file_size",383176},{"crc32_hash",3314522919u},
        {"download_url","/api/v3/tracks/7/downloads/13"},{"is_current",true}};
    auto rowFor=[](CustomSaphiCatalogue *cat,const char *version){for(size_t i=0;i<CustomSaphi_Count(cat);i++) if(!std::strcmp(CustomSaphi_Row(cat,i)->version,version)) return CustomSaphi_Row(cat,i); return (const CustomSaphiRevision*)nullptr;};
    j=withSca({sca});CHECK(parse(j,&c));CHECK(CustomSaphi_Count(c)==2);
    CHECK(rowFor(c,"1.0.0")->sca.id==13 && rowFor(c,"1.0.0")->sca.bytes==383176 && rowFor(c,"1.0.0")->sca.crc32==3314522919u);
    CHECK(!std::strcmp(rowFor(c,"1.0.0")->sca.path,"/api/v3/tracks/7/downloads/13"));
    CHECK(rowFor(c,"0.9.0")->sca.id==0 && !rowFor(c,"0.9.0")->disabledReason[0]);CustomSaphi_Free(&c);
    j=withSca({});CHECK(parse(j,&c));CHECK(rowFor(c,"1.0.0")->sca.id==0);CustomSaphi_Free(&c);
    {auto second=sca;second["id"]=14;second["download_url"]="/api/v3/tracks/7/downloads/14";
     j=withSca({sca,second});CHECK(parse(j,&c));CHECK(rowFor(c,"1.0.0")->sca.id==0 && !rowFor(c,"1.0.0")->disabledReason[0]);CustomSaphi_Free(&c);
     second["is_current"]=false;j=withSca({sca,second});CHECK(parse(j,&c));CHECK(rowFor(c,"1.0.0")->sca.id==13);CustomSaphi_Free(&c);}
    {auto bad=sca;bad["download_url"]="https://evil.test/x";j=withSca({bad});CHECK(parse(j,&c));CHECK(rowFor(c,"1.0.0")->sca.id==0);CustomSaphi_Free(&c);}
    {auto big=sca;big["file_size"]=2*1024*1024;j=withSca({big});CHECK(parse(j,&c));CHECK(rowFor(c,"1.0.0")->sca.id==0);CustomSaphi_Free(&c);}
    {auto ref=sca;ref["download_url"]=nullptr;ref["file_size"]=0;j=withSca({ref});CHECK(parse(j,&c));CHECK(rowFor(c,"1.0.0")->sca.id==0);CustomSaphi_Free(&c);}
    {auto odd=sca;odd["is_current"]="yes";j=withSca({odd});CHECK(parse(j,&c));CHECK(rowFor(c,"1.0.0")->sca.id==0);CustomSaphi_Free(&c);}
    /* Track picture: one current thumbnail goes to every row of the track,
       its listed size above the media cap or not; two, or a bad row, leave it off. */
    json picture={{"id",15},{"type","track_thumbnail"},{"version",nullptr},{"modes",json::array()},{"file_size",1960283},{"crc32_hash",194441523u},
        {"download_url","/api/v3/tracks/7/downloads/15"},{"is_current",true}};
    j=withSca({picture});CHECK(parse(j,&c));CHECK(CustomSaphi_Count(c)==2);
    CHECK(rowFor(c,"1.0.0")->picture.id==15 && rowFor(c,"0.9.0")->picture.id==15);
    CHECK(!std::strcmp(rowFor(c,"1.0.0")->picture.path,"/api/v3/tracks/7/downloads/15") && rowFor(c,"1.0.0")->sca.id==0);CustomSaphi_Free(&c);
    {auto second=picture;second["id"]=16;second["download_url"]="/api/v3/tracks/7/downloads/16";
     j=withSca({picture,second});CHECK(parse(j,&c));CHECK(rowFor(c,"1.0.0")->picture.id==0);CustomSaphi_Free(&c);}
    {auto bad=picture;bad["download_url"]="/api/v3/tracks/8/downloads/15";j=withSca({bad});CHECK(parse(j,&c));CHECK(rowFor(c,"1.0.0")->picture.id==0);CustomSaphi_Free(&c);}
    std::printf("Saphi catalogue: %d checks, %d failures\n",checks,failures);return failures!=0;
}

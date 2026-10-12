#include <libnet_dns_resolver.h>
#include <libnet_dns.h>
#include <libnet_ice_transport.h>
#include <libnet_uv.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace std::chrono_literals;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void word(libnet::dns::Bytes& out,unsigned short value){out.push_back(value>>8);out.push_back(value);}
static void record(libnet::dns::Bytes& out,const char* name,unsigned short type,const libnet::dns::Bytes& body) {
  require(libnet::dns::AppendName(out,name),"test name");word(out,type);word(out,1);
  for(unsigned i=0;i<4;++i)out.push_back(0);word(out,body.size());out.insert(out.end(),body.begin(),body.end());
}
int main()try{
  libnet::dns::Bytes packet;word(packet,42);word(packet,0x8180);word(packet,1);word(packet,3);word(packet,0);word(packet,0);
  require(libnet::dns::AppendName(packet,"stun.example"),"question");word(packet,1);word(packet,1);
  record(packet,"unrelated.example",1,{1,2,3,4});
  record(packet,"alias.example",1,{203,0,113,7});
  libnet::dns::Bytes alias;libnet::dns::AppendName(alias,"alias.example");record(packet,"stun.example",5,alias);
  const auto answers=libnet::dns::AddressAnswers(packet,"STUN.example",42,1);
  require(answers==std::vector<std::string>{"203.0.113.7"},"CNAME chain or unrelated answer filtering");
  require(libnet::dns::AddressAnswers(packet,"wrong.example",42,1).empty(),"wrong question accepted");
  require(!libnet::dns::MatchesResponse(packet,"wrong.example",42,1) &&
      !libnet::dns::MatchesResponse(packet,"stun.example",42,28) &&
      libnet::dns::MatchesResponse(packet,"stun.example",42,1),"unrelated response ended DNS query");
  require(libnet::dns::AddressAnswers(packet,"stun.example",43,1).empty(),"wrong transaction accepted");
  auto truncated=packet;truncated.pop_back();require(libnet::dns::AddressAnswers(truncated,"stun.example",42,1).empty(),"truncated record accepted");
  auto tc=packet;tc[2]|=2;require(libnet::dns::AddressAnswers(tc,"stun.example",42,1).empty(),"TC answer used without complete response");
  auto loop=packet;loop[12]=0xc0;loop[13]=12;require(libnet::dns::AddressAnswers(loop,"stun.example",42,1).empty(),"compression cycle accepted");
  require(libnet::IsSyntheticDnsAddress("198.18.0.42") && libnet::IsSyntheticDnsAddress("198.19.255.254") &&
      libnet::IsSyntheticDnsAddress("2001:2::29") && !libnet::IsSyntheticDnsAddress("162.159.207.0"),"fake DNS ranges");
  libnet::dns::Bytes fake;word(fake,42);word(fake,0x8180);word(fake,1);word(fake,1);word(fake,0);word(fake,0);
  libnet::dns::AppendName(fake,"stun.example");word(fake,1);word(fake,1);record(fake,"stun.example",1,{198,18,0,42});
  require(libnet::dns::AddressAnswers(fake,"stun.example",42,1).empty(),"fake DNS answer admitted");
#ifdef __APPLE__
  libnet::IceConfig config;config.network_path={"lo0","127.0.0.1",0};config.address_policy=libnet::IceAddressPolicy::ipv4;
  config.stun_servers={{"stun.example.invalid",3478}};config.stun_dns_budget_ms=5000;
  libnet::IceTransport transport;const auto start=std::chrono::steady_clock::now();
  require(transport.Start(config) && transport.BeginGather(),"async DNS start");
  require(std::chrono::steady_clock::now()-start<100ms,"DNS blocked the caller");
  libnet::IceAgent peer;libnet::IceConfig peer_config;peer_config.bind_address="127.0.0.1";
  peer_config.address_policy=libnet::IceAddressPolicy::ipv4;
  require(peer.Start(peer_config) && peer.Gather(1s),"answer fixture gather");
  const auto offer=peer.LocalDescription();
  require(!transport.SetRemoteDescription(offer+"a=candidate:bad 1 UDP 1 127.0.0.1 9 typ relay\r\n"),"pending DNS admitted relay");
  require(!transport.SetRemoteDescription("not an SDP"),"pending DNS admitted invalid credentials");
  require(transport.SetRemoteDescription(offer),"answer rejected while DNS pending");
  const auto stop=std::chrono::steady_clock::now();transport.Stop();
  require(std::chrono::steady_clock::now()-stop<250ms,"DNS cancellation blocked stop");
  require(!transport.Send("stopped") && !transport.SelectedPathAllowed(),"DNS worker revived a stopped transport");
  config.stun_servers={{"198.18.0.42",3478}};require(!transport.Start(config),"numeric fake STUN admitted");
  config.stun_servers={{"127.0.0.1",3478}};config.network_paths={{"missing-interface","192.0.2.1",0}};
  config.network_path={};require(!transport.Start(config),"invalid path fell back to system routing");
#endif
  std::cout<<"DNS matching/CNAME/bounds/fake ranges and asynchronous cancellation passed\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

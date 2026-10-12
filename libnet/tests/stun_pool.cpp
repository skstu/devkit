#include <libnet_ice_transport.h>
#include <libnet_stun_server.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace std::chrono_literals;
static void require(bool value, const char *why) { if (!value) throw std::runtime_error(why); }
int main() try {
  libnet::StunServer server;
  libnet::StunServerConfig service;
  bool started=false;
  for (unsigned port=43000; port<43100; port+=2) {
    service.ports={static_cast<uint16_t>(port),static_cast<uint16_t>(port+1)};
    if (!server.Start(service)) {started=true;break;}
  }
  require(started,"local Binding server unavailable");
  libnet::IceConfig config;
  config.bind_address="127.0.0.1";
  config.address_policy=libnet::IceAddressPolicy::ipv4;
  config.stun_servers={{"127.0.0.1",static_cast<uint16_t>(service.ports[0]+1000)},
                       {"127.0.0.1",service.ports[0]}};
  config.stun_gather_budget_ms=250;
  libnet::IceTransport a,b;
  libnet::IceAgent single;
  require(!single.Start(config),"single agent silently ignored the provider pool");
  require(a.Start(config) && a.Gather(3s),"pool gather failed");
  // A silent STUN provider must not hide candidates from a working provider.
  require(a.GatherStatistics().local.srflx>0,"working provider produced no mapping");
  const auto offer=a.LocalDescription();
  require(offer.find("x-sovkit-recovery")==std::string::npos,"pool claimed network migration");
  require(b.Start(config) && b.SetRemoteDescription(offer) && b.Gather(3s),"answer gather failed");
  require(a.SetRemoteDescription(b.LocalDescription()),"answer import failed");
  const auto deadline=std::chrono::steady_clock::now()+5s;
  while(std::chrono::steady_clock::now()<deadline && (!a.SelectedPathAllowed() || !b.SelectedPathAllowed())) {
    a.TakeEvents();b.TakeEvents();std::this_thread::sleep_for(10ms);
  }
  require(a.SelectedPathAllowed() && b.SelectedPathAllowed(),"pool path checks failed");
  require(a.Send("pool-a") && b.Send("pool-b"),"pool send failed");
  bool ar=false,br=false;
  while(std::chrono::steady_clock::now()<deadline && (!ar || !br)) {
    for(auto &e:a.TakeEvents()) if(e.type==libnet::IceEventType::datagram && std::string(e.datagram.begin(),e.datagram.end())=="pool-b") ar=true;
    for(auto &e:b.TakeEvents()) if(e.type==libnet::IceEventType::datagram && std::string(e.datagram.begin(),e.datagram.end())=="pool-a") br=true;
    std::this_thread::sleep_for(10ms);
  }
  require(ar && br,"pool did not deliver bilateral bytes");
  a.Stop();b.Stop();
  require(!a.Send("stopped") && !a.SelectedPathAllowed(),"stop retained an active bearer");
  config.stun_servers.resize(1);
  require(a.Start(config) && a.Gather(3s),"silent STUN discarded host candidates");
  require(a.GatherStatistics().local.host>0 && a.GatherStatistics().local.srflx==0,"silent provider test invalid");
  a.Stop();
  config.allow_relay=true;require(!a.Start(config),"pool allowed relay");
  config.allow_relay=false;config.tcp_direct=true;require(!a.Start(config),"pool allowed TCP");
  config.tcp_direct=false;config.stun_servers.push_back(config.stun_servers[0]);
  require(!a.Start(config),"duplicate provider admitted");
  std::cout<<"STUN pool: working/silent providers, shared candidates, bilateral bytes, stop and policy rejection passed\n";
  return 0;
} catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}

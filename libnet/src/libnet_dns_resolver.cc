#include "libnet_dns_resolver.h"
#include "libnet_dns.h"
#include "libnet_uv.h"
#include <openssl/rand.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#ifdef _WIN32
#include <iphlpapi.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace libnet {
namespace {
using Clock=std::chrono::steady_clock;
std::string Lower(std::string_view input) {
  std::string result(input);
  for(auto& c:result)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return result;
}
unsigned short U16(std::span<const unsigned char> bytes,size_t offset) {
  return static_cast<unsigned short>((bytes[offset]<<8)|bytes[offset+1]);
}
void Put16(dns::Bytes& bytes,unsigned short value) {
  bytes.push_back(static_cast<unsigned char>(value>>8));bytes.push_back(static_cast<unsigned char>(value));
}
#ifdef _WIN32
using Socket=SOCKET;
constexpr Socket kInvalid=INVALID_SOCKET;
void Close(Socket socket){closesocket(socket);}
#else
using Socket=int;
constexpr Socket kInvalid=-1;
void Close(Socket socket){close(socket);}
#endif
struct OwnedSocket {Socket value=kInvalid;~OwnedSocket(){if(value!=kInvalid)Close(value);}};
}

bool IsSyntheticDnsAddress(std::string_view address) {
  const auto endpoint=Endpoint::Parse(address,0);
  if(!endpoint)return false;
  if(endpoint->family()==AddressFamily::ipv4) {
    const auto ip=ntohl(reinterpret_cast<const sockaddr_in*>(endpoint->sockaddr_ptr())->sin_addr.s_addr);
    return (ip&0xfffe0000U)==0xc6120000U; // 198.18.0.0/15
  }
  const auto& ip=reinterpret_cast<const sockaddr_in6*>(endpoint->sockaddr_ptr())->sin6_addr;
  return ip.s6_addr[0]==0x20 && ip.s6_addr[1]==0x01 && ip.s6_addr[2]==0 && ip.s6_addr[3]==2 &&
    std::all_of(ip.s6_addr+4,ip.s6_addr+6,[](auto c){return c==0;}); // 2001:2::/48
}

std::vector<std::string> SystemDnsServers() {
  std::vector<std::string> output;
#ifdef _WIN32
  ULONG size=0;
  if(GetNetworkParams(nullptr,&size)!=ERROR_BUFFER_OVERFLOW || size>65536)return output;
  std::vector<unsigned char> bytes(size);
  auto* info=reinterpret_cast<FIXED_INFO*>(bytes.data());
  if(GetNetworkParams(info,&size)!=NO_ERROR)return output;
  for(auto* server=&info->DnsServerList;server && output.size()<4;server=server->Next)
    if(Endpoint::Parse(server->IpAddress.String,53))output.emplace_back(server->IpAddress.String);
#else
  std::ifstream file("/etc/resolv.conf");std::string line;
  while(output.size()<4 && std::getline(file,line)) {
    if(line.size()>1024)continue;
    std::istringstream fields(line);std::string keyword,address;
    if(fields>>keyword>>address && keyword=="nameserver" && Endpoint::Parse(address,53) &&
       std::find(output.begin(),output.end(),address)==output.end())output.push_back(address);
  }
#endif
  return output;
}

namespace dns {
bool MatchesResponse(std::span<const unsigned char> packet,std::string_view host,
    unsigned short transaction,unsigned short type) {
  if(packet.size()<12 || packet.size()>4096 || U16(packet,0)!=transaction ||
     (U16(packet,2)&0xf800)!=0x8000 || U16(packet,4)!=1)return false;
  size_t offset=12;std::string question;
  return ReadName(packet,offset,question,128) && question.size()<=253 && offset+4<=packet.size() &&
    Lower(question)==Lower(host) && U16(packet,offset)==type && U16(packet,offset+2)==1;
}
std::vector<std::string> AddressAnswers(std::span<const unsigned char> packet,
    std::string_view host,unsigned short transaction,unsigned short type) {
  if(packet.size()<12 || packet.size()>4096 || U16(packet,0)!=transaction ||
     (U16(packet,2)&0xfa0f)!=0x8000 || U16(packet,4)!=1 || U16(packet,6)>64 ||
     (type!=1 && type!=28))return {};
  size_t offset=12;std::string question;
  if(!ReadName(packet,offset,question,128) || question.size()>253 || offset+4>packet.size() ||
     Lower(question)!=Lower(host) || U16(packet,offset)!=type || U16(packet,offset+2)!=1)return {};
  offset+=4;
  struct Answer{std::string name,alias,address;};std::vector<Answer> answers;
  for(unsigned i=0;i<U16(packet,6);++i) {
    std::string name;
    if(!ReadName(packet,offset,name,128) || name.size()>253 || offset+10>packet.size())return {};
    const auto rr_type=U16(packet,offset),klass=U16(packet,offset+2),length=U16(packet,offset+8);
    offset+=10;
    if(length>packet.size()-offset)return {};
    Answer answer{Lower(name),{}, {}};
    if(klass==1 && rr_type==5) {
      auto cursor=offset;
      if(!ReadName(packet,cursor,answer.alias,128) || answer.alias.size()>253 || cursor!=offset+length)return {};
      answer.alias=Lower(answer.alias);
    } else if(klass==1 && rr_type==type && length==(type==1?4:16)) {
      char text[INET6_ADDRSTRLEN]{};
      if(!inet_ntop(type==1?AF_INET:AF_INET6,packet.data()+offset,text,sizeof(text)))return {};
      answer.address=text;
    }
    answers.push_back(std::move(answer));offset+=length;
  }
  std::set<std::string> names{Lower(host)};
  for(unsigned round=0;round<8;++round) {
    const auto before=names.size();
    for(const auto& answer:answers)if(names.contains(answer.name) && !answer.alias.empty())names.insert(answer.alias);
    if(names.size()==before)break;
  }
  std::vector<std::string> result;
  for(const auto& answer:answers)if(names.contains(answer.name) && !answer.address.empty() &&
      !IsSyntheticDnsAddress(answer.address) && std::find(result.begin(),result.end(),answer.address)==result.end()) {
    result.push_back(answer.address);if(result.size()==8)break;
  }
  return result;
}
}

std::vector<std::string> ResolveDnsOnPath(std::string_view host,const NetworkPath& path,
    const std::vector<std::string>& servers,std::chrono::milliseconds budget,
    const std::function<bool()>& cancelled) {
  if(!path.selected() || !ValidateNetworkPath(path) || servers.empty() || servers.size()>4 ||
     host.empty() || host.size()>253 || budget.count()<1 || budget.count()>10000)return {};
  if(const auto numeric=Endpoint::Parse(host,0))return IsSyntheticDnsAddress(host)?std::vector<std::string>{}:std::vector<std::string>{numeric->address()};
  const auto source=Endpoint::Parse(path.local_address,0);
  const auto family=source->family()==AddressFamily::ipv4?AF_INET:AF_INET6;
  const auto deadline=Clock::now()+budget;std::vector<std::string> result;
  // Queries share one deadline. Parallel A/AAAA and resolver requests prevent a
  // dead resolver or missing AAAA answer from consuming a budget per provider.
  struct Query {OwnedSocket socket;dns::Bytes request;unsigned short id=0,type=0;unsigned sends=1;Clock::time_point retry;};
  std::vector<std::unique_ptr<Query>> queries;
  for(const auto& address:servers) {
    const auto server=Endpoint::Parse(address,53);
    if(!server || server->family()!=source->family() || IsSyntheticDnsAddress(address))continue;
    for(unsigned short type:{1,28}) {
      auto query=std::make_unique<Query>();query->socket.value=socket(family,SOCK_DGRAM,0);
      if(query->socket.value==kInvalid || !PrepareNetworkPathSocket(path,query->socket.value,family) ||
         bind(query->socket.value,source->sockaddr_ptr(),source->sockaddr_length()) ||
         connect(query->socket.value,server->sockaddr_ptr(),server->sockaddr_length()) ||
         RAND_bytes(reinterpret_cast<unsigned char*>(&query->id),2)!=1)continue;
#ifdef _WIN32
      u_long enabled=1;if(ioctlsocket(query->socket.value,FIONBIO,&enabled))continue;
#else
      if(fcntl(query->socket.value,F_SETFL,O_NONBLOCK)<0)continue;
#endif
      query->type=type;Put16(query->request,query->id);Put16(query->request,0x100);
      Put16(query->request,1);for(unsigned i=0;i<3;++i)Put16(query->request,0);
      if(!dns::AppendName(query->request,host))continue;
      Put16(query->request,type);Put16(query->request,1);
      if(send(query->socket.value,reinterpret_cast<const char*>(query->request.data()),query->request.size(),0)<0)continue;
      query->retry=Clock::now()+budget/3;
      queries.push_back(std::move(query));
    }
  }
  while(!queries.empty() && Clock::now()<deadline && !(cancelled && cancelled())) {
    for(auto i=queries.begin();i!=queries.end();) {
      std::array<unsigned char,4097> packet{};
      const auto count=recv((*i)->socket.value,reinterpret_cast<char*>(packet.data()),packet.size(),0);
      if(count<0){
        if((*i)->sends<3 && Clock::now()>=(*i)->retry) {
          send((*i)->socket.value,reinterpret_cast<const char*>((*i)->request.data()),(*i)->request.size(),0);
          ++(*i)->sends;(*i)->retry=Clock::now()+budget/3;
        }
        ++i;continue;
      }
      const auto answers=dns::AddressAnswers({packet.data(),static_cast<size_t>(count)},host,(*i)->id,(*i)->type);
      for(const auto& answer:answers)if(std::find(result.begin(),result.end(),answer)==result.end())result.push_back(answer);
      // Only a response matching the transaction removes a query; spoofed or
      // unrelated datagrams cannot prematurely suppress a valid later answer.
      if(dns::MatchesResponse({packet.data(),static_cast<size_t>(count)},host,(*i)->id,(*i)->type))i=queries.erase(i);else ++i;
    }
    if(!queries.empty())std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return result;
}
} // namespace libnet

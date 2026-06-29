// =============================================================================
// network_checker.cpp — 网卡自检
// =============================================================================
#include "checker/checkers/network_checker.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

#include "checker/log/logger.hpp"
#include "checker/util/subprocess.h"
#include "checker/util/time_util.h"

namespace checker {

namespace {

// 枚举处于 UP 状态的非 lo 接口
std::vector<std::string> list_up_interfaces() {
  std::vector<std::string> ifs;
  int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) return ifs;

  struct ifconf ifc {};
  std::vector<char> buf(8192);
  ifc.ifc_len = buf.size();
  ifc.ifc_buf = buf.data();
  if (::ioctl(sock, SIOCGIFCONF, &ifc) != 0) {
    ::close(sock);
    return ifs;
  }

  char* p = buf.data();
  char* end = p + ifc.ifc_len;
  while (p < end) {
    auto* ifr = reinterpret_cast<struct ifreq*>(p);
    p += sizeof(*ifr);
    std::string n = ifr->ifr_name;
    if (n == "lo") continue;
    struct ifreq fl;
    std::memcpy(fl.ifr_name, ifr->ifr_name, IFNAMSIZ);
    if (::ioctl(sock, SIOCGIFFLAGS, &fl) == 0 && (fl.ifr_flags & IFF_UP)) {
      ifs.push_back(n);
    }
  }
  ::close(sock);
  return ifs;
}

}  // namespace

CheckResult NetworkChecker::run(const Context& ctx) {
  CheckResult r(name());
  Timer timer;

  auto ifs = list_up_interfaces();
  std::string iflist;
  for (const auto& n : ifs) {
    if (!iflist.empty()) iflist += ",";
    iflist += n;
  }
  r.details.emplace_back("interfaces", iflist.empty() ? "none" : iflist);

  if (ifs.empty()) {
    r.status = Status::kFail;
    r.message = "no up interface";
    r.elapsed_ms = timer.elapsed_ms();
    LOG_INFO("[network] %s (%dms)", r.message.c_str(), r.elapsed_ms);
    return r;
  }

  // ping 网关验证连通性
  const auto& gw = ctx.config.network.gateway;
  int count = ctx.config.network.ping_count;
  if (count <= 0) count = 3;
  std::string cmd = "ping -c " + std::to_string(count) + " -W 1 " + gw + " >/dev/null 2>&1";
  auto pr = run_subprocess(cmd);
  bool gw_ok = (pr.exit_code == 0);
  r.details.emplace_back("gateway", gw);
  r.details.emplace_back("gateway_reachable", gw_ok ? "yes" : "no");

  r.status = gw_ok ? Status::kPass : Status::kWarning;
  r.message = gw_ok ? "network ok" : "gateway unreachable";
  r.elapsed_ms = timer.elapsed_ms();
  LOG_INFO("[network] %s (%dms)", r.message.c_str(), r.elapsed_ms);
  return r;
}

}  // namespace checker

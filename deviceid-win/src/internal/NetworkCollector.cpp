#include "internal/NetworkCollector.h"

#include "DeviceInfoSDK/DeviceInfo.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace DeviceInfoSDK {
namespace internal {
namespace {

struct Candidate {
    ULONG if_type = 0;
    NET_IFINDEX if_index = 0;
    std::string adapter_name;
    bool has_gateway = false;
    bool hardware = false;
    bool connector_present = false;
    bool row_available = false;
    ULONG metric = (std::numeric_limits<ULONG>::max)();
    unsigned char physical_address[8]{};
    ULONG physical_address_length = 0;
};

bool IsKnownType(ULONG if_type) noexcept {
    return if_type == IF_TYPE_ETHERNET_CSMACD || if_type == IF_TYPE_IEEE80211;
}

std::string TypeName(ULONG if_type) {
    if (if_type == IF_TYPE_ETHERNET_CSMACD) {
        return "ethernet";
    }
    if (if_type == IF_TYPE_IEEE80211) {
        return "wifi";
    }
    return "unknown";
}

ULONG AdapterMetric(const IP_ADAPTER_ADDRESSES* adapter) noexcept {
    ULONG metric = (std::numeric_limits<ULONG>::max)();
    if (adapter->Ipv4Metric != 0) {
        metric = (std::min)(metric, adapter->Ipv4Metric);
    }
    if (adapter->Ipv6Metric != 0) {
        metric = (std::min)(metric, adapter->Ipv6Metric);
    }
    return metric;
}

bool BetterCandidate(const Candidate& left, const Candidate& right) {
    if (left.has_gateway != right.has_gateway) {
        return left.has_gateway;
    }
    if (left.hardware != right.hardware) {
        return left.hardware;
    }
    if (IsKnownType(left.if_type) != IsKnownType(right.if_type)) {
        return IsKnownType(left.if_type);
    }
    if (left.metric != right.metric) {
        return left.metric < right.metric;
    }
    if (left.if_index != right.if_index) {
        return left.if_index < right.if_index;
    }
    return left.adapter_name < right.adapter_name;
}

std::string FormatMac(const unsigned char* bytes, ULONG length) {
    if (bytes == nullptr || length != 6) {
        return {};
    }
    char text[32]{};
    std::snprintf(
        text,
        sizeof(text),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        bytes[0],
        bytes[1],
        bytes[2],
        bytes[3],
        bytes[4],
        bytes[5]);
    return text;
}

} // namespace

NetworkResult CollectNetwork(bool collect_mac) noexcept {
    NetworkResult result;
    ULONG buffer_size = 15u * 1024u;
    std::vector<unsigned char> buffer(buffer_size);
    const ULONG flags =
        GAA_FLAG_INCLUDE_GATEWAYS |
        GAA_FLAG_SKIP_ANYCAST |
        GAA_FLAG_SKIP_MULTICAST |
        GAA_FLAG_SKIP_DNS_SERVER;

    ULONG status = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3; ++attempt) {
        buffer.assign(buffer_size, 0);
        auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        status = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &buffer_size);
        if (status != ERROR_BUFFER_OVERFLOW) {
            break;
        }
        if (buffer_size == 0 || buffer_size > 1024u * 1024u) {
            result.native_error = ERROR_INSUFFICIENT_BUFFER;
            return result;
        }
    }

    if (status == ERROR_NO_DATA) {
        result.ok = true;
        return result;
    }
    if (status != ERROR_SUCCESS) {
        result.native_error = status;
        return result;
    }

    std::vector<Candidate> candidates;
    for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
         adapter != nullptr;
         adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp ||
            adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
            adapter->IfType == IF_TYPE_TUNNEL ||
            adapter->FirstUnicastAddress == nullptr) {
            continue;
        }

        MIB_IFROW row{};
        row.dwIndex = adapter->IfIndex;
        const DWORD row_status = GetIfEntry(&row);
        if (row_status == NO_ERROR) {
            if (row.dwOperStatus != IF_OPER_STATUS_OPERATIONAL &&
                row.dwOperStatus != IF_OPER_STATUS_CONNECTED) {
                continue;
            }
        }

        Candidate candidate;
        candidate.if_type = adapter->IfType;
        candidate.if_index = adapter->IfIndex != 0 ? adapter->IfIndex : adapter->Ipv6IfIndex;
        candidate.adapter_name = adapter->AdapterName != nullptr ? adapter->AdapterName : "";
        candidate.has_gateway = adapter->FirstGatewayAddress != nullptr;
        candidate.metric = AdapterMetric(adapter);
        candidate.row_available = row_status == NO_ERROR;
        candidate.hardware = IsKnownType(adapter->IfType);
        candidate.connector_present = candidate.hardware;
        candidate.physical_address_length = adapter->PhysicalAddressLength;
        const ULONG copy_len = (std::min)(adapter->PhysicalAddressLength, static_cast<ULONG>(sizeof(candidate.physical_address)));
        std::copy(adapter->PhysicalAddress, adapter->PhysicalAddress + copy_len, candidate.physical_address);
        candidates.push_back(candidate);
    }

    result.ok = true;
    if (candidates.empty()) {
        return result;
    }

    std::sort(candidates.begin(), candidates.end(), BetterCandidate);
    const Candidate& selected = candidates.front();
    result.has_candidate = true;
    result.network_type = TypeName(selected.if_type);
    if (!selected.row_available || !selected.hardware) {
        result.diagnostic_flags |= kDiagVirtualAdapterPossible;
    }
    if (collect_mac && selected.physical_address_length == 6) {
        result.mac = FormatMac(selected.physical_address, selected.physical_address_length);
    }
    return result;
}

} // namespace internal
} // namespace DeviceInfoSDK

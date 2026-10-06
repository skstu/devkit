/* Exclusive single-socket filtering experiment; included under conn_lock.
 * Six serial transactions: baseline A; A->B x3; contact B; A->B.
 * Never learn ICE candidates from these messages; cookie is private and short lived.
 */
#include "crc32.h"

static void sovkit_filter_put16(uint8_t *p, uint16_t n) { p[0] = n >> 8; p[1] = n; }
static void sovkit_filter_put32(uint8_t *p, uint32_t n) {
	for (int i = 3; i >= 0; --i) { p[i] = n; n >>= 8; }
}
static uint32_t sovkit_filter_get32(const uint8_t *p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void sovkit_filtering_send(juice_agent_t *agent, int index) {
	juice_sovkit_mapping_sample_t *s = &agent->sovkit_filtering.samples[index];
	uint8_t packet[64] = {0};
	sovkit_filter_put16(packet, 1);
	sovkit_filter_put16(packet + 2, 44);
	sovkit_filter_put32(packet + 4, 0x2112a442);
	memcpy(packet + 8, s->transaction_id, 12);
	sovkit_filter_put16(packet + 20, 0xc0a1);
	sovkit_filter_put16(packet + 22, 20);
	packet[24] = 1;
	packet[25] = index == 0 || index == 4 ? 1 : 2;
	if (packet[25] == 2) memcpy(packet + 28, agent->sovkit_filtering_cookie, 16);
	sovkit_filter_put16(packet + 44, 0x8026);
	sovkit_filter_put16(packet + 46, 8);
	sovkit_filter_put16(packet + 56, 0x8028);
	sovkit_filter_put16(packet + 58, 4);
	sovkit_filter_put32(packet + 60, CRC32(packet, 56) ^ 0x5354554e);
	addr_record_t target;
	int ret = sovkit_mapping_target(agent, s->server_port, &target)
	    ? agent_direct_send(agent, &target, (char *)packet, sizeof(packet), 0) : -1;
	++s->sends;
	s->send_status = ret < 0 ? ret : 0;
	if (ret < 0) s->state = 4;
}

static void sovkit_filtering_update(juice_agent_t *agent, timestamp_t *next_timestamp) {
	if (!agent->config.sovkit_filtering_second_port) return;
	juice_sovkit_mapping_stats_t *stats = &agent->sovkit_filtering;
	uint64_t now = (uint64_t)current_timestamp();
	if (!stats->state) {
		stats->started_ms = stats->last_tick_ms = now;
		addr_record_t target;
		if (agent->config.sovkit_mapping_second_port ||
		    agent->config.sovkit_filtering_second_port == agent->config.stun_server_port ||
		    !sovkit_mapping_target(agent, agent->config.stun_server_port, &target) ||
		    !sovkit_mapping_target(agent, agent->config.sovkit_filtering_second_port, &target)) {
			stats->state = 4; return;
		}
		stats->state = 1;
		for (int i = 0; i < 6; ++i) {
			juice_random(stats->samples[i].transaction_id, 12);
			stats->samples[i].server_port = i == 4 ? agent->config.sovkit_filtering_second_port
			                                            : agent->config.stun_server_port;
		}
	}
	if (stats->state != 1) return;
	if (now - stats->last_tick_ms > 2500) { stats->state = 3; return; }
	stats->last_tick_ms = now;
	uint64_t age = now - stats->started_ms;
	int index = 0;
	while (index < 6 && stats->samples[index].state >= 2) ++index;
	if (index == 6) { stats->state = 2; return; }
	// No usable return-route proof: do not attempt cross-port traffic.
	if (index > 0 && stats->samples[0].state != 2) { stats->state = 2; return; }
	juice_sovkit_mapping_sample_t *s = &stats->samples[index];
	// Preserve observable phase order even on sub-millisecond loopback paths.
	if (!s->state && index > 0) {
		const juice_sovkit_mapping_sample_t *previous = &stats->samples[index - 1];
		uint64_t ended = previous->state == 2 ? previous->received_ms : previous->sent_ms + 2000;
		if (age < ended + 100) {
			if (*next_timestamp > (timestamp_t)(now + 50)) *next_timestamp = (timestamp_t)(now + 50);
			return;
		}
	}
	if (!s->state) {
		s->state = 1; s->sent_ms = age;
		sovkit_filtering_send(agent, index);
	} else if (age - s->sent_ms >= 2000) s->state = 3;
	else if (s->sends == 1 && age - s->sent_ms >= 500) sovkit_filtering_send(agent, index);
	if (*next_timestamp > (timestamp_t)(now + 50)) *next_timestamp = (timestamp_t)(now + 50);
}

static bool sovkit_filtering_receive(juice_agent_t *agent, char *buf, size_t len,
                                      const addr_record_t *src) {
	juice_sovkit_mapping_stats_t *stats = &agent->sovkit_filtering;
	if (!agent->config.sovkit_filtering_second_port || !stats->state ||
	    len < 20 || !is_stun_datagram(buf, len)) return false;
	for (int i = 0; i < 6; ++i) {
		juice_sovkit_mapping_sample_t *s = &stats->samples[i];
		if (memcmp(buf + 8, s->transaction_id, 12)) continue;
		uint64_t age = (uint64_t)current_timestamp() - stats->started_ms;
		addr_record_t numeric = *src, expected;
		addr_unmap_inet6_v4mapped((struct sockaddr *)&numeric.addr, &numeric.len);
		uint16_t response_port = i == 0 ? agent->config.stun_server_port
		                                  : agent->config.sovkit_filtering_second_port;
		const uint8_t *b = (const uint8_t *)buf;
		stun_message_t msg;
		if (len != 64 || b[0] != 1 || b[1] != 1 || b[2] != 0 || b[3] != 44 ||
		    b[20] != 0 || b[21] != 0x20 || b[22] != 0 || b[23] != 8 || b[24] || b[25] != 1 ||
		    b[32] != 0xc0 || b[33] != 0xa1 || b[34] != 0 || b[35] != 20 ||
		    b[36] != 1 || b[37] != (i == 0 || i == 4 ? 1 : 2) || b[38] || b[39] ||
		    b[56] != 0x80 || b[57] != 0x28 || b[58] != 0 || b[59] != 4 ||
		    sovkit_filter_get32(b + 60) != (CRC32(b, 56) ^ 0x5354554e) ||
		    !sovkit_mapping_target(agent, response_port, &expected) ||
		    !addr_record_is_equal(&numeric, &expected, true) ||
		    (i != 0 && i != 4 && memcmp(b + 40, agent->sovkit_filtering_cookie, 16)) ||
		    stun_read(buf, len, &msg) < 0 || !msg.has_fingerprint ||
		    msg.mapped.addr.ss_family != AF_INET ||
		    !addr_get_port((const struct sockaddr *)&msg.mapped.addr) ||
		    !((const struct sockaddr_in *)&msg.mapped.addr)->sin_addr.s_addr ||
		    ntohl(((const struct sockaddr_in *)&msg.mapped.addr)->sin_addr.s_addr) >= 0xe0000000U) {
			++stats->rejected_responses; return true;
		}
		if (s->state == 2) { ++stats->duplicate_responses; return true; }
		if (stats->state != 1 || s->state != 1 || age - s->sent_ms >= 2000) {
			++stats->rejected_responses; return true;
		}
		if (i == 0) memcpy(agent->sovkit_filtering_cookie, b + 40, 16);
		s->mapped_ipv4 = ((const struct sockaddr_in *)&msg.mapped.addr)->sin_addr.s_addr;
		s->mapped_port = addr_get_port((const struct sockaddr *)&msg.mapped.addr);
		s->received_ms = age; s->state = 2;
		return true;
	}
	return false;
}

// An exported diagnostic offer must not let incoming authenticated peer checks
// trigger traffic to B. Only the ordinary gather transaction from A may enter ICE.
static bool sovkit_filtering_allow_gather(juice_agent_t *agent, char *buf, size_t len,
                                          const addr_record_t *src) {
	if (!agent->config.sovkit_filtering_second_port) return true;
	if (len < 20 || !is_stun_datagram(buf, len) || (uint8_t)buf[0] != 1 ||
	    ((uint8_t)buf[1] != 1 && (uint8_t)buf[1] != 0x11)) return false;
	addr_record_t expected, numeric = *src;
	addr_unmap_inet6_v4mapped((struct sockaddr *)&numeric.addr, &numeric.len);
	if (!sovkit_mapping_target(agent, agent->config.stun_server_port, &expected) ||
	    !addr_record_is_equal(&numeric, &expected, true)) return false;
	agent_stun_entry_t *entry = agent_find_entry_from_transaction_id(agent, (uint8_t *)buf + 8);
	return entry && entry->type == AGENT_STUN_ENTRY_TYPE_SERVER;
}

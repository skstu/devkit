/* Opt-in PCP v2 MAP, UDP only, one mapping for this ICE socket. RFC 6887.
 * All callbacks execute under conn_lock. No THIRD_PARTY, wildcard or relay.
 * Bound IPv4 and gateway are explicitly supplied by the local user/SDK.
 */
static void sovkit_pcp_put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v; }
static void sovkit_pcp_put32(uint8_t *p, uint32_t v) {
	for (int i = 3; i >= 0; --i) { p[i] = v; v >>= 8; }
}
static uint32_t sovkit_pcp_get32(const uint8_t *p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static bool sovkit_pcp_global(uint32_t network_ip) {
	uint32_t ip = ntohl(network_ip), first = ip >> 24;
	return first && first != 10 && first != 127 && first < 224 &&
	    (ip & 0xfff00000U) != 0xac100000U && (ip & 0xffff0000U) != 0xc0a80000U &&
	    (ip & 0xffff0000U) != 0xa9fe0000U && (ip & 0xffc00000U) != 0x64400000U &&
	    (ip & 0xfffe0000U) != 0xc6120000U && (ip & 0xffffff00U) != 0xc0000200U &&
	    (ip & 0xffffff00U) != 0xc6336400U && (ip & 0xffffff00U) != 0xcb007100U;
}
static addr_record_t sovkit_pcp_address(uint32_t ip, uint16_t port) {
	addr_record_t out;
	memset(&out, 0, sizeof(out));
	out.len = sizeof(struct sockaddr_in);
	out.socktype = SOCK_DGRAM;
	struct sockaddr_in *v = (struct sockaddr_in *)&out.addr;
	v->sin_family = AF_INET; v->sin_addr.s_addr = ip; v->sin_port = htons(port);
	return out;
}
static void sovkit_pcp_send(juice_agent_t *agent, bool remove) {
	juice_sovkit_pcp_stats_t *s = &agent->sovkit_pcp;
	if (!s->internal_port) return;
	uint8_t packet[60] = {0};
	packet[0] = 2; packet[1] = 1;
	sovkit_pcp_put32(packet + 4, remove ? 0 : 120);
	packet[18] = packet[19] = 0xff;
	memcpy(packet + 20, &agent->config.sovkit_pcp_local_ipv4, 4);
	memcpy(packet + 24, agent->sovkit_pcp_nonce, 12);
	packet[36] = 17;
	sovkit_pcp_put16(packet + 40, s->internal_port);
	sovkit_pcp_put16(packet + 42, s->external_port);
	if (agent->sovkit_pcp_external_ipv4) {
		packet[54] = packet[55] = 0xff;
		memcpy(packet + 56, &agent->sovkit_pcp_external_ipv4, 4);
	}
	addr_record_t target = sovkit_pcp_address(agent->config.sovkit_pcp_gateway_ipv4, 5351);
	if (!remove) {
		if (!agent->sovkit_pcp_pending) agent->sovkit_pcp_request_started = (uint64_t)current_timestamp();
		agent->sovkit_pcp_pending = true;
	}
	int ret = agent_direct_send(agent, &target, (const char *)packet, sizeof(packet), 0);
	if (!remove) { ++s->requests_sent; s->send_status = ret < 0 ? ret : 0; }
}
static void sovkit_pcp_stop(juice_agent_t *agent) {
	if (!agent->config.sovkit_pcp_gateway_ipv4) return;
	agent->sovkit_pcp_stopping = true;
	// Also cancel an unacknowledged create: the gateway may have lost its reply.
	if (agent->sovkit_pcp.requests_sent) sovkit_pcp_send(agent, true);
	memset(agent->sovkit_pcp_nonce, 0, sizeof(agent->sovkit_pcp_nonce));
	memset(agent->config.sovkit_pcp_nonce, 0, sizeof(agent->config.sovkit_pcp_nonce));
}
static void sovkit_pcp_update(juice_agent_t *agent, timestamp_t *next_timestamp) {
	if (!agent->config.sovkit_pcp_gateway_ipv4 || agent->sovkit_pcp_stopping) return;
	juice_sovkit_pcp_stats_t *s = &agent->sovkit_pcp;
	uint64_t now = (uint64_t)current_timestamp();
	if (!s->state) {
		s->state = 1;
		agent->sovkit_pcp_deadline = now + 6500;
	}
	if (s->state == 1 && !s->internal_port) {
		for (int i = 0; i < agent->local.candidates_count; ++i) {
			ice_candidate_t *c = agent->local.candidates + i;
			addr_record_t record = c->resolved;
			addr_unmap_inet6_v4mapped((struct sockaddr *)&record.addr, &record.len);
			if (c->type == ICE_CANDIDATE_TYPE_HOST && record.addr.ss_family == AF_INET &&
			    ((struct sockaddr_in *)&record.addr)->sin_addr.s_addr == agent->config.sovkit_pcp_local_ipv4)
				s->internal_port = addr_get_port((struct sockaddr *)&record.addr);
		}
		// conn_create starts the thread before gather publishes host candidates.
		if (!s->internal_port) {
			if (now >= agent->sovkit_pcp_deadline) s->state = 8;
			else if (*next_timestamp > (timestamp_t)(now + 25)) *next_timestamp = (timestamp_t)(now + 25);
			return;
		}
		// The wrapper uses OpenSSL RAND_bytes and fails closed if it fails.
		// juice_random is suitable only for non-security timing jitter here.
		memcpy(agent->sovkit_pcp_nonce, agent->config.sovkit_pcp_nonce, 12);
		memset(agent->config.sovkit_pcp_nonce, 0, 12);
		agent->sovkit_pcp_next_send = now;
	}
	if (s->state != 1 && s->state != 2) return;
	if (s->state == 2 && now >= agent->sovkit_pcp_expires) {
		s->state = 6; return;
	}
	if (s->state == 1 && now >= agent->sovkit_pcp_deadline) {
		s->state = 5; return;
	}
	if (now >= agent->sovkit_pcp_next_send) {
		sovkit_pcp_send(agent, false);
		if (s->send_status < 0 && s->state == 1) { s->state = 8; return; }
		// Bounded initial experiment: two randomized ~3s attempts, not an
		// unbounded PCP retry loop. Renewal retries are at least 4s apart.
		uint16_t random;
		juice_random(&random, sizeof(random));
		agent->sovkit_pcp_next_send = now + (s->state == 1 ? 3000 + random % 301 :
		    (agent->sovkit_pcp_expires - now) / 2 > 4000 ? (agent->sovkit_pcp_expires - now) / 2 : 4000);
		if (s->state == 1 && s->requests_sent >= 2) agent->sovkit_pcp_next_send = agent->sovkit_pcp_deadline;
	}
	uint64_t next = agent->sovkit_pcp_next_send;
	uint64_t end = s->state == 1 ? agent->sovkit_pcp_deadline : agent->sovkit_pcp_expires;
	if (next > end) next = end;
	if (*next_timestamp > (timestamp_t)next) *next_timestamp = (timestamp_t)next;
}
static bool sovkit_pcp_receive(juice_agent_t *agent, const char *data, size_t len,
                              const addr_record_t *source) {
	if (!agent->config.sovkit_pcp_gateway_ipv4 || !len || (uint8_t)data[0] != 2) return false;
	juice_sovkit_pcp_stats_t *s = &agent->sovkit_pcp;
	const uint8_t *p = (const uint8_t *)data;
	addr_record_t expected = sovkit_pcp_address(agent->config.sovkit_pcp_gateway_ipv4, 5351), src = *source;
	addr_unmap_inet6_v4mapped((struct sockaddr *)&src.addr, &src.len);
	uint64_t now = (uint64_t)current_timestamp();
	if (len != 60 || !addr_record_is_equal(&src, &expected, true) || p[1] != 0x81 ||
	    memcmp(p + 24, agent->sovkit_pcp_nonce, 12) || p[36] != 17 ||
	    ((uint16_t)p[40] << 8 | p[41]) != s->internal_port ||
	    !agent->sovkit_pcp_pending ||
	    (s->state != 1 && s->state != 2) || agent->sovkit_pcp_stopping ||
	    (s->state == 1 && now >= agent->sovkit_pcp_deadline) ||
	    (s->state == 2 && now >= agent->sovkit_pcp_expires)) {
		++s->rejected_responses; return true;
	}
	s->result_code = p[3];
	if (p[3]) {
		agent->sovkit_pcp_pending = false;
		// A failed refresh does not revoke the previously granted lease.
		// Retry on the native timer; never extend the lease on an error.
		if (s->state == 1) s->state = p[3] == 1 || p[3] == 4 ? 3 : 4;
		return true;
	}
	uint32_t lifetime = sovkit_pcp_get32(p + 4), ip;
	memcpy(&ip, p + 56, 4);
	uint16_t port = (uint16_t)p[42] << 8 | p[43];
	static const uint8_t mapped_prefix[12] = {0,0,0,0,0,0,0,0,0,0,0xff,0xff};
	if (memcmp(p + 44, mapped_prefix, 12) || !port || lifetime < 30 || lifetime > 3600 ||
	    !sovkit_pcp_global(ip)) {
		// Do not advertise private/upstream-NAT mappings as reachable public endpoints.
		++s->rejected_responses;
		if (s->state == 1) { s->state = 4; sovkit_pcp_send(agent, true); }
		return true;
	}
	// Conservatively count lease time from the earliest outstanding request,
	// not a possibly delayed/replayed response. Only one refresh is outstanding.
	uint64_t expires = agent->sovkit_pcp_request_started + (uint64_t)lifetime * 1000;
	if (expires <= now) { ++s->rejected_responses; return true; }
	agent->sovkit_pcp_pending = false;
	if (s->state == 2 && (ip != agent->sovkit_pcp_external_ipv4 || port != s->external_port)) {
		// A signed non-trickle package cannot be mutated after issuance.
		s->state = 7; sovkit_pcp_send(agent, true); return true;
	}
	if (s->state == 2) ++s->renewals;
	else {
		addr_record_t mapped = sovkit_pcp_address(ip, port);
		if (agent_add_local_reflexive_candidate(agent, ICE_CANDIDATE_TYPE_SERVER_REFLEXIVE, &mapped)) {
			s->state = 8; sovkit_pcp_send(agent, true); return true;
		}
	}
	agent->sovkit_pcp_external_ipv4 = ip;
	s->external_port = port; s->lifetime_seconds = lifetime; s->state = 2;
	agent->sovkit_pcp_expires = expires;
	uint16_t random;
	juice_random(&random, sizeof(random));
	agent->sovkit_pcp_next_send = now + (expires - now) * (500 + random % 126) / 1000;
	return true;
}

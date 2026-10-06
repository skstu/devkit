/* SovKit diagnostic extension. Included by agent.c; all calls hold conn_lock.
 * Never adds ICE candidates, changes roles, authenticates peers, or uses TURN.
 * Independent A/B/A/B/A/B samples, one retry at 500 ms, deadline 2 s each.
 */
static bool sovkit_mapping_target(juice_agent_t *agent, uint16_t port, addr_record_t *target) {
	memset(target, 0, sizeof(*target));
	struct sockaddr_in *ip = (struct sockaddr_in *)&target->addr;
	ip->sin_family = AF_INET;
	ip->sin_port = htons(port);
	target->len = sizeof(*ip);
	target->socktype = SOCK_DGRAM;
	return agent->config.stun_server_host && port >= 1024 &&
	       inet_pton(AF_INET, agent->config.stun_server_host, &ip->sin_addr) == 1 &&
	       ntohl(ip->sin_addr.s_addr) != 0 && ntohl(ip->sin_addr.s_addr) < 0xe0000000U;
}

static void sovkit_mapping_send(juice_agent_t *agent, juice_sovkit_mapping_sample_t *sample) {
	addr_record_t target;
	if (!sovkit_mapping_target(agent, sample->server_port, &target)) {
		sample->state = 4;
		sample->send_status = -1;
		return;
	}
	stun_message_t msg;
	memset(&msg, 0, sizeof(msg));
	msg.msg_class = STUN_CLASS_REQUEST;
	msg.msg_method = STUN_METHOD_BINDING;
	memcpy(msg.transaction_id, sample->transaction_id, 12);
	char buffer[256];
	int size = stun_write(buffer, sizeof(buffer), &msg, NULL);
	int ret = size > 0 ? agent_direct_send(agent, &target, buffer, (size_t)size, 0) : -1;
	++sample->sends;
	sample->send_status = ret < 0 ? ret : 0;
	if (ret < 0) sample->state = 4;
}

static void sovkit_mapping_update(juice_agent_t *agent, timestamp_t *next_timestamp) {
	if (!agent->config.sovkit_mapping_second_port) return;
	juice_sovkit_mapping_stats_t *stats = &agent->sovkit_mapping;
	uint64_t now = (uint64_t)current_timestamp();
	if (!stats->state) {
		stats->started_ms = stats->last_tick_ms = now;
		addr_record_t target;
		if (agent->config.sovkit_mapping_second_port == agent->config.stun_server_port ||
		    !sovkit_mapping_target(agent, agent->config.stun_server_port, &target) ||
		    !sovkit_mapping_target(agent, agent->config.sovkit_mapping_second_port, &target)) {
			stats->state = 4;
			return;
		}
		stats->state = 1;
		for (int i = 0; i < JUICE_SOVKIT_MAPPING_SAMPLES; ++i) {
			juice_random(stats->samples[i].transaction_id, 12);
			stats->samples[i].server_port = i % 2 ? agent->config.sovkit_mapping_second_port
			                                              : agent->config.stun_server_port;
		}
	}
	if (stats->state != 1) return;
	// Do not catch up in a burst after suspension, or classify a long-gap sample.
	if (now - stats->last_tick_ms > 2500) { stats->state = 3; return; }
	stats->last_tick_ms = now;
	uint64_t age = now - stats->started_ms;
	bool complete = true;
	for (int i = 0; i < JUICE_SOVKIT_MAPPING_SAMPLES; ++i) {
		juice_sovkit_mapping_sample_t *s = &stats->samples[i];
		if (!s->state && age >= (uint64_t)i * 1000) {
			s->state = 1;
			s->sent_ms = age;
			sovkit_mapping_send(agent, s);
		} else if (s->state == 1) {
			if (age - s->sent_ms >= 2000) s->state = 3;
			else if (s->sends == 1 && age - s->sent_ms >= 500) sovkit_mapping_send(agent, s);
		}
		if (s->state < 2) complete = false;
	}
	if (complete) stats->state = 2;
	else if (*next_timestamp > (timestamp_t)(now + 100)) *next_timestamp = (timestamp_t)(now + 100);
}

static bool sovkit_mapping_receive(juice_agent_t *agent, char *buf, size_t len,
                                    const addr_record_t *src) {
	juice_sovkit_mapping_stats_t *stats = &agent->sovkit_mapping;
	if (!agent->config.sovkit_mapping_second_port || !stats->state ||
	    len < sizeof(struct stun_header) || !is_stun_datagram(buf, len)) return false;
	const struct stun_header *header = (const struct stun_header *)buf;
	for (int i = 0; i < JUICE_SOVKIT_MAPPING_SAMPLES; ++i) {
		juice_sovkit_mapping_sample_t *s = &stats->samples[i];
		if (memcmp(header->transaction_id, s->transaction_id, 12)) continue;
		// Own transactions never enter ICE's candidate/check machinery, including late replies.
		uint64_t age = (uint64_t)current_timestamp() - stats->started_ms;
		addr_record_t target;
		addr_record_t numeric = *src;
		addr_unmap_inet6_v4mapped((struct sockaddr *)&numeric.addr, &numeric.len);
		stun_message_t msg;
		// Controlled Binding-only response: exactly one XOR-MAPPED-ADDRESS and
		// a terminal FINGERPRINT. Reject duplicate/unknown attributes, credential
		// responses and trailing bytes instead of accepting an ambiguous mapping.
		if (len != 40 || ntohs(header->length) != 20 ||
		    (uint8_t)buf[20] != 0 || (uint8_t)buf[21] != 0x20 ||
		    (uint8_t)buf[22] != 0 || (uint8_t)buf[23] != 8 ||
		    (uint8_t)buf[24] != 0 || (uint8_t)buf[25] != 1 ||
		    (uint8_t)buf[32] != 0x80 || (uint8_t)buf[33] != 0x28 ||
		    (uint8_t)buf[34] != 0 || (uint8_t)buf[35] != 4 ||
		    !sovkit_mapping_target(agent, s->server_port, &target) ||
		    !addr_record_is_equal(&numeric, &target, true) || stun_read(buf, len, &msg) < 0 ||
		    msg.msg_class != STUN_CLASS_RESP_SUCCESS || msg.msg_method != STUN_METHOD_BINDING ||
		    !msg.has_fingerprint || msg.mapped.addr.ss_family != AF_INET ||
		    !addr_get_port((const struct sockaddr *)&msg.mapped.addr) ||
		    !((const struct sockaddr_in *)&msg.mapped.addr)->sin_addr.s_addr ||
		    ntohl(((const struct sockaddr_in *)&msg.mapped.addr)->sin_addr.s_addr) >= 0xe0000000U) {
			++stats->rejected_responses;
			return true;
		}
		if (s->state == 2) { ++stats->duplicate_responses; return true; }
		if (stats->state != 1 || s->state != 1 || age - s->sent_ms >= 2000) {
			++stats->rejected_responses;
			return true;
		}
		s->mapped_ipv4 = ((const struct sockaddr_in *)&msg.mapped.addr)->sin_addr.s_addr;
		s->mapped_port = addr_get_port((const struct sockaddr *)&msg.mapped.addr);
		s->received_ms = age;
		s->state = 2;
		return true;
	}
	return false;
}

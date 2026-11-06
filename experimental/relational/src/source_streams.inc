void Session::projectStream(const Output& output,const std::string& parameters,const std::string& arguments,
                            unsigned offset,unsigned bytes,unsigned capacity,std::ostream& header,std::ostream& code) {
    const auto symbol="vx_"+output.name;
    header<<"/* Stream API: token 0 means backpressure/exhaustion; leases transfer result ownership. */\n"
        <<"uint32_t "<<symbol<<"_submit("<<parameters<<") VX_ABI VX_RESIDENT;\n"
        <<"uint32_t "<<symbol<<"_read_lease(uint32_t lease, uint16_t index) VX_ABI VX_RESIDENT;\n"
        <<"uint8_t "<<symbol<<"_release(uint32_t lease) VX_ABI VX_RESIDENT;\n";
    code<<"uint32_t "<<symbol<<"_read(uint16_t index) VX_ABI VX_RESIDENT { return "<<symbol<<"_read_at(index, 0); }\n"
        <<"static uint32_t "<<symbol<<"_leases["<<capacity<<"];\n"
        <<"uint32_t "<<symbol<<"_submit("<<parameters<<") VX_ABI VX_RESIDENT {\n"
        <<"    uint8_t slot; uint32_t lease;\n";
    if(bytes) code<<"    uint16_t n, destination;\n";
    code<<"    if (vx_stream_running || vx_stream_sequence == 0xffffffffUL) return 0;\n"
        <<"    for (slot = 0; slot < "<<capacity<<"; ++slot) if (!"<<symbol<<"_leases[slot]) break;\n"
        <<"    if (slot == "<<capacity<<") return 0;\n"
        <<"    vx_stream_running = 1; lease = ++vx_stream_sequence;\n    "<<symbol<<"("<<arguments<<");\n";
    if(bytes) code<<"    destination = "<<offset<<"u + ((uint16_t)slot + 1u) * "<<bytes<<"u;\n"
        <<"    for (n = 0; n < "<<bytes<<"u; ++n) vx_payload[destination+n] = vx_payload["<<offset<<"u+n];\n";
    code<<"    "<<symbol<<"_leases[slot] = lease; vx_stream_running = 0; return lease;\n}\n"
        <<"uint8_t "<<symbol<<"_release(uint32_t lease) VX_ABI VX_RESIDENT {\n"
        <<"    uint8_t slot; if (!lease) return 0;\n"
        <<"    for (slot = 0; slot < "<<capacity<<"; ++slot) if ("<<symbol<<"_leases[slot] == lease) { "<<symbol<<"_leases[slot] = 0; return 1; }\n"
        <<"    return 0;\n}\n"
        <<"uint32_t "<<symbol<<"_read_lease(uint32_t lease, uint16_t index) VX_ABI VX_RESIDENT {\n"
        <<"    uint8_t slot; if (lease) for (slot = 0; slot < "<<capacity<<"; ++slot) if ("<<symbol<<"_leases[slot] == lease)\n"
        <<"        return "<<symbol<<"_read_at(index, ((uint16_t)slot + 1u) * "<<bytes<<"u);\n"
        <<"    vx_trap(21); return 0;\n}\n";
}

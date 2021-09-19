#!/usr/bin/env python3

import sys

TCP_STAT_DESC = [
    [
        "wscale",
        "sep",
        ",",
        [
            ["snd_wscale", "int"],
            ["rcv_wscale", "int"],
        ],
    ],
    ["rto", "double"],
    ["backoff", "int"],
    [
        "rtt",
        "sep",
        "/",
        [
            ["rtt", "double"],
            ["rttvar", "double"],
        ],
    ],
    ["ato", "double"],
    ["qack", "int"],
    ["mss", "int"],
    ["pmtu", "uint"],
    ["rcvmss", "int"],
    ["advmss", "int"],
    ["cwnd", "uint"],
    ["ssthresh", "int"],
    ["bytes_sent", "uint"],
    ["bytes_retrans", "uint"],
    ["bytes_acked", "uint"],
    ["bytes_received", "uint"],
    ["segs_out", "uint"],
    ["segs_in", "uint"],
    ["data_segs_out", "uint"],
    ["data_segs_in", "uint"],
    [
        "bbr",
        "composite",
        [
            ["bw", "bps"],
            ["mrtt", "double"],
            ["pacing_gain", "double"],
            ["cwnd_gain", "double"],
        ],
    ],
    ["send", "nextfield", "bps"],
    ["lastsnd", "uint"],
    ["lastrcv", "uint"],
    ["lastack", "uint"],
    [
        "pacing_rate",
        "nextfield",
        "maybesep",
        "/",
        [
            ["pacing_rate", "bps"],
            ["pacing_rate_max", "bps"],
        ],
    ],
    ["delivery_rate", "nextfield", "bps"],
    ["delivered", "uint"],
    ["delivered_ce", "uint"],
    ["app_limited", "flag"],
    ["busy", "ms"],
    ["rwnd_limited", "ms+junk"],
    ["sndbuf_limited", "ms+junk"],
    ["unacked", "uint"],
    [
        "retrans",
        "sep",
        "/",
        [
            ["retrans", "uint"],
            ["retrans_total", "uint"],
        ],
    ],
    ["lost", "uint"],
    ["sacked", "uint"],
    ["dsack_dups", "uint"],
    ["fackets", "uint"],
    ["reordering", "int"],
    ["reord_seen", "int"],
    ["rcv_rtt", "double"],
    ["rcv_space", "int"],
    ["rcv_ssthresh", "uint"],
    ["notsent", "uint"],
    ["minrtt", "double"],
]

IS_AUX_KEEP_NAME = {
    "snd_wscale",
    "rcv_wscale",
    "backoff",
    "qack",
    "mss",
    "pmtu",
    "rcvmss",
    "advmss",
    "cwnd",
    "ssthresh",
    "bytes_retrans",
    "bytes_acked",
    "bytes_received",
    "segs_out",
    "segs_in",
    "data_segs_out",
    "data_segs_in",
    "pacing_rate",
    "pacing_rate_max",
    "delivery_rate",
    "delivered",
    "delivered_ce",
    "app_limited",
    "unacked",
    "retrans",
    "retrans_total",
    "lost",
    "sacked",
    "dsack_dups",
    "fackets",
    "reordering",
    "reord_seen",
    "rcv_space",
    "rcv_ssthresh",
}

RENAME_MAP_IS_AUX_NAME = {
    "ato": (True, "ato_ms"),
    "busy": (True, "busy_time_ms"),
    "bytes_sent": (False, "cum_usage_bytes"),
    "lastsnd": (True, "lastsnd_ms"),
    "lastrcv": (True, "lastrcv_ms"),
    "lastack": (True, "lastack_ms"),
    "minrtt": (True, "min_rtt_ms"),
    "notsent": (True, "not_sent"),
    "rcv_rtt": (True, "rcv_rtt_ms"),
    "rto": (True, "rto_ms"),
    "rtt": (True, "rtt_ms"),
    "rttvar": (True, "rtt_var_ms"),
    "rwnd_limited": (True, "rwnd_limited_ms"),
    "sndbuf_limited": (True, "sndbuf_limited_ms"),
    "send": (False, "cur_usage_bps"),
    "bbr:bw": (True, "bbr_bw"),
    "bbr:mrtt": (True, "bbr_min_rtt_ms"),
    "bbr:pacing_gain": (True, "bbr_pacing_gain"),
    "bbr:cwnd_gain": (True, "bbr_cwnd_gain"),
}


def tr_name(fname):
    if fname in IS_AUX_KEEP_NAME:
        return (True, fname)
    if fname in RENAME_MAP_IS_AUX_NAME:
        return RENAME_MAP_IS_AUX_NAME[fname]
    raise ValueError(fname)


def field_is_aux(fname):
    if fname == "wscale":
        return True
    elif fname == "bbr":
        return True
    return tr_name(fname)[0]


def gen_check_aux(is_aux, parse_field):
    if is_aux:
        return """if (aux != nullptr) {{\n{0}\n}}""".format(parse_field)
    return parse_field


def gen_strip_prefix_then(skip_rest_once_done, prefix, parse_field):
    return """if (absl::StartsWith(field, "{0}")) {{
        field = absl::StripPrefix(field, "{0}");
        {1}
        {2}
        }}""".format(prefix, parse_field,
                     "continue;" if skip_rest_once_done else "")


def assignment_stmt(dst, val_name):
    if dst[0] == True:
        return "aux->set_" + dst[1] + "(" + val_name + ")"
    elif dst[0] == False:
        return dst[1] + " = " + val_name
    else:
        raise ValueError(dst)


def gen_parse_int(dst, name):
    return """int64_t got_val = 0;
    if (!absl::SimpleAtoi(field, &got_val)) {{
      return absl::InvalidArgumentError("failed to parse {1} field");
    }}
    {0};""".format(assignment_stmt(dst, "got_val"), name)


def gen_parse_uint(dst, name):
    return """uint64_t got_val = 0;
    if (!absl::SimpleAtoi(field, &got_val)) {{
      return absl::InvalidArgumentError("failed to parse {1} field");
    }}
    {0};""".format(assignment_stmt(dst, "got_val"), name)


def gen_parse_ms(has_junk, dst, name):
    return """int64_t got_val = 0;
    if (!ParseMsSS({2}, field, &got_val)) {{
      return absl::InvalidArgumentError("failed to parse {1} field");
    }}
    {0};""".format(assignment_stmt(dst, "got_val"), name,
                   "true" if has_junk else "false")


def gen_parse_bps(dst, name):
    return """int64_t got_val = 0;
    if (!ParseBpsSS(field, &got_val)) {{
      return absl::InvalidArgumentError("failed to parse {1} field");
    }}
    {0};""".format(assignment_stmt(dst, "got_val"), name)


def gen_parse_double(dst, name):
    return """double got_val = 0;
    if (!absl::SimpleAtod(field, &got_val)) {{
      return absl::InvalidArgumentError(absl::StrCat("failed to parse {1} field from '", field, "'"));
    }}
    {0};""".format(assignment_stmt(dst, "got_val"), name)


def gen_set_flag(continue_once_done, field_name, dst):
    return """if (field == "{0}") {{ {1}; {2} }}""".format(
        field_name, assignment_stmt(dst, "true"),
        "continue;" if continue_once_done else "")


def gen_next_field_vardef(field_name):
    return "bool next_is_" + field_name + " = false;"


def gen_next_field_late(field_name):
    return """if (field == "{0}") {{ next_is_{0} = true; continue; }}""".format(
        field_name)


def gen_next_field_early(field_name, pcode):
    return """if (next_is_{0}) {{\n{1}\nnext_is_{0} = false;\ncontinue;\n}}""".format(
        field_name, pcode)


def gen_parse_composite(field_name, sub_fields):
    s = """
      if (!absl::StartsWith(field, "(") || !absl::EndsWith(field, ")")) {{
          return absl::InvalidArgumentError("bad composite value for {2} field");
      }}
      field = field.substr(1, field.size()-2);
      std::vector<std::string_view> sub_fields =
      absl::StrSplit(field, "{0}", absl::SkipWhitespace());
      for (std::string_view field : sub_fields) {{
    """.format(",", len(sub_fields), field_name)
    for f in sub_fields:
        fmt = """
            if (absl::StartsWith(field, "{0}:")) {{
                field = absl::StripPrefix(field, "{0}:");
                {1}
                continue;
            }}
        """
        if f[1] == "bps":
            s += fmt.format(
                f[0], gen_parse_bps(tr_name(field_name + ":" + f[0]), f[0]))
        elif f[1] == "double":
            s += fmt.format(
                f[0], gen_parse_double(tr_name(field_name + ":" + f[0]), f[0]))
        else:
            raise NotImplementedError
    s += "}"
    return s


def gen_parse_sep(sep, sub_fields, name):
    s = """std::vector<std::string_view> sub_fields =
      absl::StrSplit(field, "{0}", absl::SkipWhitespace());
      if (sub_fields.size() != {1}) {{
        return absl::InvalidArgumentError("failed to parse {2} field");
      }}
    """.format(sep, len(sub_fields), name)
    i = 0
    for f in sub_fields:
        if f[1] == "bps":
            s += ("{{\nstd::string_view field = sub_fields[{0}];".format(i) +
                  gen_parse_bps(tr_name(f[0]), name) + "\n}")
        elif f[1] == "double":
            s += ("{{\nstd::string_view field = sub_fields[{0}];".format(i) +
                  gen_parse_double(tr_name(f[0]), name) + "\n}")
        elif f[1] == "int":
            s += ("{{\nstd::string_view field = sub_fields[{0}];".format(i) +
                  gen_parse_int(tr_name(f[0]), name) + "\n}")
        elif f[1] == "uint":
            s += ("{{\nstd::string_view field = sub_fields[{0}];".format(i) +
                  gen_parse_uint(tr_name(f[0]), name) + "\n}")
        else:
            raise NotImplementedError
        i += 1
    return s


def gen_parse_maybesep(sep, sub_fields, name):
    s = """std::vector<std::string_view> sub_fields =
      absl::StrSplit(field, "{0}", absl::SkipWhitespace());
      if (sub_fields.size() < 1) {{
        return absl::InvalidArgumentError("failed to parse {2} field");
      }}
    """.format(sep, len(sub_fields), name)
    i = 0
    for f in sub_fields:
        if f[1] == "bps":
            s += "if (sub_fields.size() >= {0}) {{\nstd::string_view field = sub_fields[{1}];".format(
                i + 1, i) + gen_parse_bps(tr_name(f[0]), name) + "\n}"
        elif f[1] == "double":
            s += "if (sub_fields.size() >= {0}) {{\nstd::string_view field = sub_fields[{1}];".format(
                i + 1, i) + gen_parse_double(tr_name(f[0]), name) + "\n}"
        elif f[1] == "int":
            s += "if (sub_fields.size() >= {0}) {{\nstd::string_view field = sub_fields[{1}];".format(
                i + 1, i) + gen_parse_int(tr_name(f[0]), name) + "\n}"
        elif f[1] == "uint":
            s += "if (sub_fields.size() >= {0}) {{\nstd::string_view field = sub_fields[{1}];".format(
                i + 1, i) + gen_parse_uint(tr_name(f[0]), name) + "\n}"
        else:
            raise NotImplementedError
        i += 1
    return s


def gen_parser(fout):
    vardefs = []
    early_parsing = []
    late_parsing = []
    for field in TCP_STAT_DESC:
        if field[1] == "bps":
            late_parsing.append((field[0],
                                 gen_check_aux(
                                     field_is_aux(field[0]),
                                     gen_strip_prefix_then(
                                         True, field[0] + ":",
                                         gen_parse_bps(tr_name(field[0]),
                                                       field[0])))))
        elif field[1] == "composite":
            late_parsing.append(
                (field[0],
                 gen_check_aux(
                     field_is_aux(field[0]),
                     gen_strip_prefix_then(
                         True, field[0] + ":",
                         gen_parse_composite(field[0], field[2])))))
        elif field[1] == "double":
            late_parsing.append(
                (field[0],
                 gen_check_aux(
                     field_is_aux(field[0]),
                     gen_strip_prefix_then(
                         True, field[0] + ":",
                         gen_parse_double(tr_name(field[0]), field[0])))))
        elif field[1] == "flag":
            late_parsing.append(
                (field[0],
                 gen_check_aux(field_is_aux(field[0]),
                               gen_set_flag(True, field[0],
                                            tr_name(field[0])))))
        elif field[1] == "int":
            late_parsing.append((field[0],
                                 gen_check_aux(
                                     field_is_aux(field[0]),
                                     gen_strip_prefix_then(
                                         True, field[0] + ":",
                                         gen_parse_int(tr_name(field[0]),
                                                       field[0])))))
        elif field[1] == "ms":
            late_parsing.append((field[0],
                                 gen_check_aux(
                                     field_is_aux(field[0]),
                                     gen_strip_prefix_then(
                                         True, field[0] + ":",
                                         gen_parse_ms(False, tr_name(field[0]),
                                                      field[0])))))
        elif field[1] == "ms+junk":
            late_parsing.append((field[0],
                                 gen_check_aux(
                                     field_is_aux(field[0]),
                                     gen_strip_prefix_then(
                                         True, field[0] + ":",
                                         gen_parse_ms(True, tr_name(field[0]),
                                                      field[0])))))
        elif field[1] == "nextfield":
            vardefs.append(gen_next_field_vardef(field[0]))
            late_parsing.append((field[0],
                                 gen_check_aux(field_is_aux(field[0]),
                                               gen_next_field_late(field[0]))))
            if field[2] == "bps":
                early_parsing.append(
                    gen_next_field_early(
                        field[0], gen_parse_bps(tr_name(field[0]), field[0])))
            elif field[2] == "maybesep":
                early_parsing.append(
                    gen_next_field_early(
                        field[0],
                        gen_parse_maybesep(field[3], field[4], field[0])))
            else:
                raise NotImplementedError
        elif field[1] == "sep":
            late_parsing.append((field[0],
                                 gen_check_aux(
                                     field_is_aux(field[0]),
                                     gen_strip_prefix_then(
                                         True, field[0] + ":",
                                         gen_parse_sep(field[2], field[3],
                                                       field[0])))))
        elif field[1] == "uint":
            late_parsing.append(
                (field[0],
                 gen_check_aux(
                     field_is_aux(field[0]),
                     gen_strip_prefix_then(
                         True, field[0] + ":",
                         gen_parse_uint(tr_name(field[0]), field[0])))))
        else:
            late_parsing.append((field[0], "// TODO: parse field " + field[0]))

    late_parsing.sort()

    fout.write("""
absl::Status ParseFieldsSS(const std::vector<std::string_view>& fields, int64_t& cur_usage_bps,
                           int64_t& cum_usage_bytes, proto::FlowInfo::AuxInfo* aux) {{
  {vardefs}
  for (std::string_view field : fields) {{
      {early_parsing}
      switch (field[0]) {{
""".format(vardefs="\n".join(sorted(vardefs)),
           early_parsing="\n".join(sorted(early_parsing))))
    cur_case = ""
    for pcode in late_parsing:
        if pcode[0][0] != cur_case:
            if cur_case != "":
                fout.write("      break;\n    }\n")
            cur_case = pcode[0][0]
            fout.write("    case '{0}': {{\n".format(cur_case))
        fout.write(pcode[1])
        fout.write("\n")
    if cur_case != "":
        fout.write("    }\n")
    fout.write("    }\n  }\nreturn absl::OkStatus();\n}\n")


gen_parser(sys.stdout)

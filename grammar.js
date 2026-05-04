module.exports = grammar({
  name: 'org_inline',
  externals: $ => [
    $._bold_open,      $._bold_close,
    $._italic_open,    $._italic_close,
    $._underline_open, $._underline_close,
    $._strike_open,    $._strike_close,
    $._verbatim_token,
    $._code_token,
    $._plain_text_token,
    $._link_regular_token,
    $._link_plain_token,
    $._link_angle_token,
    $._link_radio_token,
    $._ts_open_active,
    $._ts_close_active,
    $._ts_open_inactive,
    $._ts_close_inactive,
    $._ts_date_token,
    $._ts_dayname_token,
    $._ts_time_token,
    $._ts_time_range_token,
    $._ts_repeater_token,
    $._ts_repeater_alarm_token,
    $._ts_repeater_filter_token,
    $._ts_warning_token,
    $._ts_diary_token,
    $._ts_range_separator,
    $._citation_token,
    $._citation_open,
    $._citation_style_token,
    $._citation_colon,
    $._citation_text_token,
    $._citation_key_token,
    $._citation_separator,
    $._citation_close,
    $._macro_token,
    $._inline_src_block_token,
    $._export_snippet_token,
    $._footnote_ref_token,
    $._target_token,
    $._statistics_cookie_token,
    $._line_break_token,
    $._subscript_token,
    $._superscript_token,
    $._entity_token,
    $._latex_fragment_token,
    $._inline_babel_call_token,
  ],
  extras: _ => [],
  rules: {
    inline_content: $ => repeat($._inline_object),

    _inline_object: $ => choice(
      $.bold, $.italic, $.underline, $.strike,
      $.verbatim, $.code,
      $.link_regular, $.link_plain, $.link_angle, $.link_radio,
      $.timestamp_range_active, $.timestamp_range_inactive,
      $.timestamp_active, $.timestamp_inactive, $.timestamp_diary,
      $.citation,
      $.macro,
      $.inline_src_block,
      $.export_snippet,
      $.footnote_ref,
      $.target,
      $.statistics_cookie,
      $.line_break,
      $.subscript,
      $.superscript,
      $.entity,
      $.latex_fragment,
      $.inline_babel_call,
      $.plain_text,
    ),

    bold:      $ => seq($._bold_open,      repeat($._inline_object), $._bold_close),
    italic:    $ => seq($._italic_open,    repeat($._inline_object), $._italic_close),
    underline: $ => seq($._underline_open, repeat($._inline_object), $._underline_close),
    strike:    $ => seq($._strike_open,    repeat($._inline_object), $._strike_close),

    verbatim: $ => $._verbatim_token,
    code:     $ => $._code_token,

    /* `_link_regular_token` is emitted by the C scanner covering only
     * the leading `[[`. The JS rule below consumes the rest, exposing
     * `target` and (optional) `description` as named children. */
    link_regular: $ => seq(
      $._link_regular_token,
      field('target', $.link_target),
      optional(seq('][', field('description', $.link_description))),
      ']]',
    ),

    /* Link target: text up to either `]]` or `][`. `[^\]\n]+` eats
     * everything except `]` and newline. The C scanner's validation
     * guarantees a `]]` exists before end-of-line. */
    link_target:      $ => /[^\]\n]+/,
    link_description: $ => /[^\]\n]*/,

    /* Plain URL link (`https://example.com`). Single token, but we
     * alias it as a `target` field for consistency with link_regular. */
    link_plain: $ => field('target',
      alias($._link_plain_token, $.link_target)),

    /* Angle-bracketed link (`<https://example.com>`). The brackets are
     * part of the token text — consumers can strip them or use the
     * full text as-is. */
    link_angle: $ => field('target',
      alias($._link_angle_token, $.link_target)),

    /* Radio link: text matching a defined `<<<radio_target>>>`. */
    link_radio: $ => field('target',
      alias($._link_radio_token, $.link_target)),

    timestamp_active: $ => seq(
      $._ts_open_active,
      field('date', $.ts_date),
      optional(field('dayname', $.ts_dayname)),
      optional(field('time', choice($.ts_time, $.ts_time_range))),
      optional(field('repeater', $.ts_repeater)),
      optional(field('alarm',    $.ts_repeater_alarm)),
      optional(field('filter',   $.ts_repeater_filter)),
      optional(field('warning',  $.ts_warning)),
      $._ts_close_active,
    ),

    timestamp_inactive: $ => seq(
      $._ts_open_inactive,
      field('date', $.ts_date),
      optional(field('dayname', $.ts_dayname)),
      optional(field('time', choice($.ts_time, $.ts_time_range))),
      optional(field('repeater', $.ts_repeater)),
      optional(field('alarm',    $.ts_repeater_alarm)),
      optional(field('filter',   $.ts_repeater_filter)),
      optional(field('warning',  $.ts_warning)),
      $._ts_close_inactive,
    ),

    ts_date:             $ => $._ts_date_token,
    ts_dayname:          $ => $._ts_dayname_token,
    ts_time:             $ => $._ts_time_token,
    ts_time_range:       $ => $._ts_time_range_token,
    ts_repeater:         $ => $._ts_repeater_token,
    ts_repeater_alarm:   $ => $._ts_repeater_alarm_token,
    ts_repeater_filter:  $ => $._ts_repeater_filter_token,
    ts_warning:          $ => $._ts_warning_token,

    timestamp_diary:     $ => $._ts_diary_token,

    timestamp_range_active: $ => seq(
      $.timestamp_active,
      $._ts_range_separator,
      $.timestamp_active,
    ),
    timestamp_range_inactive: $ => seq(
      $.timestamp_inactive,
      $._ts_range_separator,
      $.timestamp_inactive,
    ),

    citation: $ => seq(
      $._citation_open,
      optional($.citation_style),
      $._citation_colon,
      $.citation_reference,
      repeat(seq($._citation_separator, $.citation_reference)),
      $._citation_close,
    ),
    citation_reference: $ => seq(
      optional($.citation_prefix),
      $.citation_key,
      optional($.citation_suffix),
    ),
    citation_style:     $ => $._citation_style_token,
    citation_prefix:    $ => $._citation_text_token,
    citation_suffix:    $ => $._citation_text_token,
    citation_key:       $ => $._citation_key_token,

    /* `_macro_token` is emitted by the C scanner covering only the
     * leading `{{{`. JS rules decompose into `name` and optional
     * comma-separated `arguments`. */
    macro: $ => seq(
      $._macro_token,
      field('name', $.macro_name),
      optional(seq(
        '(',
        optional(seq(
          field('argument', $.macro_argument),
          repeat(seq(',', field('argument', $.macro_argument))),
        )),
        ')',
      )),
      '}}}',
    ),

    macro_name:     $ => /[A-Za-z][A-Za-z0-9_-]*/,
    macro_argument: $ => /[^,)]+/,

    /* Inline source block `src_LANG[ARGS]{BODY}`. Scanner emits
     * `_inline_src_block_token` covering `src_`; JS rules pull out
     * the language, optional header args (in `[]`), and body (in `{}`). */
    inline_src_block: $ => seq(
      $._inline_src_block_token,
      field('language', $.inline_src_language),
      optional(seq(
        '[',
        field('header_args', $.inline_src_args),
        ']',
      )),
      '{',
      field('body', $.inline_src_body),
      '}',
    ),

    inline_src_language: $ => /[A-Za-z][A-Za-z0-9_+-]*/,
    inline_src_args:     $ => /[^\]\n]*/,
    inline_src_body:     $ => /[^}\n]*/,

    export_snippet: $ => $._export_snippet_token,

    /* Footnote reference. Three surface forms:
     *   `[fn:label]`        — labelled
     *   `[fn:label:body]`   — labelled with inline definition
     *   `[fn::body]`        — anonymous (empty label) inline definition
     * Scanner emits `_footnote_ref_token` covering only `[fn:`. */
    footnote_ref: $ => seq(
      $._footnote_ref_token,
      optional(field('label', $.footnote_label)),
      optional(seq(':', field('body', $.footnote_body))),
      ']',
    ),

    footnote_label: $ => /[A-Za-z0-9_-]+/,
    footnote_body:  $ => /[^\]\n]+/,

    target: $ => $._target_token,

    statistics_cookie: $ => $._statistics_cookie_token,
    line_break:        $ => $._line_break_token,
    subscript:         $ => $._subscript_token,
    superscript:       $ => $._superscript_token,

    entity:           $ => $._entity_token,
    latex_fragment:   $ => $._latex_fragment_token,
    /* Inline babel call `call_NAME[INSIDE_HDR](ARGS)[END_HDR]`. */
    inline_babel_call: $ => seq(
      $._inline_babel_call_token,
      field('name', $.inline_call_name),
      optional(seq('[', field('inside_header', $.inline_call_args), ']')),
      '(',
      field('arguments', $.inline_call_args),
      ')',
      optional(seq('[', field('end_header', $.inline_call_args), ']')),
    ),

    inline_call_name: $ => /[A-Za-z][A-Za-z0-9_-]*/,
    inline_call_args: $ => /[^\]\n)]*/,

    plain_text: $ => $._plain_text_token,
  },
});

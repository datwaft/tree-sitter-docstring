#include "tree_sitter/alloc.h"
#include "tree_sitter/parser.h"

#include <stdint.h>
#include <string.h>

enum TokenType {
  ARGS_HEADER,
  RETURNS_HEADER,
  YIELDS_HEADER,
  RAISES_HEADER,
  ATTRIBUTES_HEADER,
  KEYWORD_ARGS_HEADER,
  OTHER_PARAMETERS_HEADER,
  WARNS_HEADER,
  EXAMPLES_HEADER,
  NOTES_HEADER,
  WARNING_HEADER,
  ADMONITION_HEADER,
  SEE_ALSO_HEADER,
  REFERENCES_HEADER,
  TODO_HEADER,
  METHODS_HEADER,
  SECTION_END,
  ENTRY_END,
  IDENTIFIER,
  EXCEPTION_NAME,
  PARENTHESIZED_TYPE,
  RETURN_TYPE,
  CODE_END,
  FENCE_START,
  FENCE_END,
  CODE_DIRECTIVE,
  DIRECTIVE_OPTION,
  CODE_CONTENT,
  DOCTEST_PROMPT,
  CODE_LINE,
  ENTRY_START,
  TEXT_START,
  TEXT_FRAGMENT,
  TEXT_END,
  LITERAL_START,
  EMPHASIS_START,
  STRONG_START,
  INTERPRETED_START,
  ROLE_START,
  REFERENCE_START,
  SIMPLE_REFERENCE,
  INLINE_CONTENT,
  INLINE_CLOSE,
  ROLE_SUFFIX,
  URL,
  ERROR_SENTINEL,
};

typedef struct {
  uint32_t section_indent;
  uint32_t entry_indent;
  // Layout tokens leave the lexer after indentation, which may contain tabs.
  uint32_t pending_indent;
  uint32_t literal_indent;
  uint8_t section;
  bool in_entry;
  uint32_t block_indent;
  uint32_t fence_length;
  uint32_t doctest_indent;
  uint8_t block_mode; // 1: fenced block; 2: reStructuredText directive.
  char fence_character;
  uint8_t doctest_mode; // 0: inactive; 1: input; 2: expected output.
  bool doctest_code;
  bool entry_name_pending;
  bool in_text;
  bool inline_enabled;
  bool text_line_start;
  bool text_space;
  bool text_escaped;
  uint32_t text_indent;
  uint32_t text_colons;
  int32_t text_previous;
  uint32_t inline_length;
  uint8_t inline_close;
} Scanner;

static bool horizontal_space(int32_t c) {
  return c == ' ' || c == '\t';
}

static bool line_end(TSLexer *lexer) {
  return lexer->eof(lexer) || lexer->lookahead == '\n' || lexer->lookahead == '\r';
}

static void skip_horizontal_space(TSLexer *lexer) {
  while (horizontal_space(lexer->lookahead)) {
    lexer->advance(lexer, false);
  }
}

static bool name_start(int32_t c) {
  // Non-ASCII names are preserved without trying to validate Python identifiers.
  return c == '_' || (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') || c >= 0x80;
}

static bool name_continue(int32_t c) {
  return name_start(c) || (c >= '0' && c <= '9');
}

// The outer '(' has already been consumed. Leave its matching ')' to the grammar.
static bool scan_parenthesized_type(TSLexer *lexer) {
  uint32_t depth = 0;
  int32_t quote = 0;
  bool escaped = false;
  while (!line_end(lexer)) {
    int32_t c = lexer->lookahead;
    if (quote) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == quote) {
        quote = 0;
      }
    } else if (c == '\'' || c == '"' || c == '`') {
      quote = c;
    } else if (c == '(') {
      depth++;
    } else if (c == ')') {
      if (depth == 0) return true;
      depth--;
    }
    lexer->advance(lexer, false);
  }
  return false;
}

// An atom may be an arbitrary name, a reference, or balanced type syntax.
static bool scan_return_type_atom(TSLexer *lexer) {
  skip_horizontal_space(lexer);
  bool has_content = false;
  if (lexer->lookahead == ':') {
    lexer->advance(lexer, false);
    int32_t previous = 0;
    while (name_continue(lexer->lookahead) || lexer->lookahead == ':' ||
           lexer->lookahead == '-' || lexer->lookahead == '+') {
      previous = lexer->lookahead;
      lexer->advance(lexer, false);
    }
    if (previous != ':' || lexer->lookahead != '`') return false;
  } else if (name_start(lexer->lookahead)) {
    while (name_continue(lexer->lookahead) || lexer->lookahead == '.') {
      lexer->advance(lexer, false);
    }
    has_content = true;
  }

  for (;;) {
    skip_horizontal_space(lexer);
    int32_t c = lexer->lookahead;
    if (c != '(' && c != '[' && c != '{' && c != '\'' && c != '"' && c != '`') {
      return has_content;
    }
    uint32_t depth = 0;
    int32_t quote = 0;
    bool escaped = false;
    do {
      if (line_end(lexer)) return false;
      c = lexer->lookahead;
      if (quote) {
        if (escaped) {
          escaped = false;
        } else if (c == '\\') {
          escaped = true;
        } else if (c == quote) {
          quote = 0;
        }
      } else if (c == '\'' || c == '"' || c == '`') {
        quote = c;
      } else if (c == '(' || c == '[' || c == '{') {
        depth++;
      } else if ((c == ')' || c == ']' || c == '}') && depth) {
        depth--;
      }
      lexer->advance(lexer, false);
    } while (depth || quote);
    has_content = true;
  }
}

// Ordinary multiword prose is ambiguous; only explicit type syntax wins over text.
static bool scan_return_type(TSLexer *lexer, unsigned *trailing_colons) {
  while (scan_return_type_atom(lexer)) {
    if (lexer->lookahead == ':') {
      lexer->mark_end(lexer);
      lexer->advance(lexer, false);
      // A reStructuredText literal marker is not a type separator.
      if (lexer->lookahead == ':') {
        *trailing_colons = 1;
        return false;
      }
      return true;
    }
    if (lexer->lookahead == '|' || lexer->lookahead == ',') {
      lexer->advance(lexer, false);
      continue;
    }
    char connector[4] = {0};
    unsigned length = 0;
    while (name_continue(lexer->lookahead)) {
      if (length < sizeof(connector) - 1) {
        connector[length] = lexer->lookahead < 0x80 ? (char)lexer->lookahead : '?';
      }
      length++;
      lexer->advance(lexer, false);
    }
    if (length >= sizeof(connector) ||
        (strcmp(connector, "of") != 0 && strcmp(connector, "or") != 0 &&
         strcmp(connector, "and") != 0)) {
      return false;
    }
  }
  return false;
}

// A block body is one token, so headings and punctuation inside it stay code.
static bool scan_code_block(Scanner *scanner, TSLexer *lexer, const bool *valid) {
  if (lexer->lookahead == '\r') lexer->advance(lexer, true);
  if (lexer->lookahead == '\n') lexer->advance(lexer, true);
  bool has_content = false;
  lexer->mark_end(lexer);
  while (!lexer->eof(lexer)) {
    uint32_t indent = lexer->get_column(lexer);
    while (horizontal_space(lexer->lookahead)) {
      indent = lexer->lookahead == '\t' ? (indent / 8 + 1) * 8 : indent + 1;
      lexer->advance(lexer, false);
    }
    if (lexer->lookahead == '\r' || lexer->lookahead == '\n') {
      if (lexer->lookahead == '\r') lexer->advance(lexer, !has_content);
      if (lexer->lookahead == '\n') lexer->advance(lexer, !has_content);
      lexer->mark_end(lexer);
      continue;
    }
    if (scanner->block_mode == 1 && lexer->lookahead == scanner->fence_character) {
      unsigned length = 0;
      while (lexer->lookahead == scanner->fence_character) {
        lexer->advance(lexer, false);
        length++;
      }
      skip_horizontal_space(lexer);
      if (length >= scanner->fence_length && line_end(lexer)) {
        if (has_content && valid[CODE_CONTENT]) {
          lexer->result_symbol = CODE_CONTENT;
          return true;
        }
        if (valid[FENCE_END]) {
          lexer->mark_end(lexer);
          scanner->block_mode = 0;
          lexer->result_symbol = FENCE_END;
          return true;
        }
        return false;
      }
    }
    bool dedented = scanner->block_mode == 1 ? indent < scanner->block_indent
                                           : indent <= scanner->block_indent;
    if (dedented) {
      if (has_content && valid[CODE_CONTENT]) {
        lexer->result_symbol = CODE_CONTENT;
        return true;
      }
      if (valid[CODE_END]) {
        scanner->block_mode = 0;
        lexer->result_symbol = CODE_END;
        return true;
      }
      return false;
    }
    if (!has_content && scanner->block_mode == 2 && lexer->lookahead == ':' &&
        valid[DIRECTIVE_OPTION]) {
      while (!line_end(lexer)) lexer->advance(lexer, false);
      lexer->mark_end(lexer);
      lexer->result_symbol = DIRECTIVE_OPTION;
      return true;
    }
    while (!line_end(lexer)) lexer->advance(lexer, false);
    if (lexer->lookahead == '\r') lexer->advance(lexer, false);
    if (lexer->lookahead == '\n') lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    has_content = true;
  }
  if (has_content && valid[CODE_CONTENT]) {
    lexer->result_symbol = CODE_CONTENT;
    return true;
  }
  if (valid[CODE_END]) {
    scanner->block_mode = 0;
    lexer->result_symbol = CODE_END;
    return true;
  }
  return false;
}

// reST delimiters need word boundaries. In particular, arithmetic and Python
// identifiers containing stars or underscores are ordinary prose.
static bool inline_before(int32_t c) {
  return c == 0 || horizontal_space(c) || c == '(' || c == '[' || c == '{' ||
         c == '<' || c == '\'' || c == '"' || c == ':' || c == '-' || c == '/';
}

static bool inline_after(int32_t c) {
  return inline_before(c) || c == ')' || c == ']' || c == '}' || c == '>' ||
         c == '.' || c == ',' || c == ';' || c == '!' || c == '?' ||
         c == '\n' || c == '\r';
}

// Probe a complete pair before emitting its opener. Failed probes leave the
// marked end on plain source, so unfinished edits stay plain text.
// Stay within one prose line so an unfinished opener cannot absorb a section.
static bool scan_inline_pair(Scanner *scanner, TSLexer *lexer, int32_t marker,
                             unsigned width, enum TokenType token) {
  if (line_end(lexer) || horizontal_space(lexer->lookahead)) return false;
  uint32_t length = 0;
  int32_t previous = 0;
  bool escaped = false;
  while (!line_end(lexer)) {
    int32_t c = lexer->lookahead;
    if (c == marker && !escaped) {
      unsigned count = 0;
      while (lexer->lookahead == marker) {
        lexer->advance(lexer, false);
        count++;
      }
      if (count == width && length && !horizontal_space(previous)) {
        unsigned close = width;
        enum TokenType matched = token;
        if (marker == '`' && width == 1 && token != ROLE_START && lexer->lookahead == '_') {
          lexer->advance(lexer, false);
          close++;
          if (lexer->lookahead == '_') { lexer->advance(lexer, false); close++; }
          matched = REFERENCE_START;
        }
        if (inline_after(lexer->lookahead) || lexer->eof(lexer) ||
            (marker == '`' && width == 1 && lexer->lookahead == ':')) {
          scanner->inline_length = length;
          scanner->inline_close = (uint8_t)close;
          scanner->text_colons = 0;
          scanner->text_space = false;
          scanner->text_escaped = false;
          lexer->result_symbol = matched;
          return true;
        }
        count = close;
      }
      length += count;
      previous = marker;
      continue;
    }
    if (width == 2 && marker == '`') {
      escaped = false; // Backslashes are literal inside inline literals.
    } else {
      escaped = !escaped && c == '\\';
    }
    previous = c;
    length++;
    lexer->advance(lexer, false);
  }
  return false;
}

static void text_character(Scanner *scanner, int32_t c) {
  if (c == ':') {
    scanner->text_colons = scanner->text_space ? 1 : scanner->text_colons + 1;
    scanner->text_space = false;
  } else if (horizontal_space(c)) {
    scanner->text_space = true;
  } else {
    scanner->text_colons = 0;
    scanner->text_space = false;
  }
  scanner->text_escaped = !scanner->text_escaped && c == '\\';
  scanner->text_previous = c;
}

static bool scan_text(Scanner *scanner, TSLexer *lexer, const bool *valid) {
  if (valid[INLINE_CONTENT] && scanner->inline_length) {
    for (uint32_t i = 0; i < scanner->inline_length; i++) {
      if (line_end(lexer)) return false;
      lexer->advance(lexer, false);
    }
    scanner->inline_length = 0;
    lexer->mark_end(lexer);
    lexer->result_symbol = INLINE_CONTENT;
    return true;
  }
  if (valid[INLINE_CLOSE] && scanner->inline_close) {
    for (unsigned i = 0; i < scanner->inline_close; i++) {
      if (line_end(lexer)) return false;
      scanner->text_previous = lexer->lookahead;
      lexer->advance(lexer, false);
    }
    scanner->inline_close = 0;
    lexer->mark_end(lexer);
    lexer->result_symbol = INLINE_CLOSE;
    return true;
  }
  if (valid[ROLE_SUFFIX] && lexer->lookahead == ':') {
    // An ordinary prose colon after interpreted text is still a text fragment.
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    bool has_name = name_start(lexer->lookahead);
    int32_t previous = 0;
    while (name_continue(lexer->lookahead) || lexer->lookahead == ':' ||
           lexer->lookahead == '-' || lexer->lookahead == '+' || lexer->lookahead == '.') {
      if (previous == ':' && inline_after(lexer->lookahead)) break;
      previous = lexer->lookahead;
      lexer->advance(lexer, false);
    }
    if (has_name && previous == ':') {
      lexer->mark_end(lexer);
      scanner->text_previous = ':';
      lexer->result_symbol = ROLE_SUFFIX;
      return true;
    }
    if (!valid[TEXT_FRAGMENT]) return false;
    text_character(scanner, ':');
    lexer->result_symbol = TEXT_FRAGMENT;
    return true;
  }
  if (!valid[TEXT_FRAGMENT]) return false;
  if (line_end(lexer)) {
    scanner->in_text = false;
    if (scanner->inline_enabled && scanner->text_line_start && scanner->text_colons >= 2) {
      scanner->literal_indent = scanner->text_indent;
    }
    lexer->result_symbol = TEXT_END;
    return valid[TEXT_END];
  }
  bool consumed = false;
  while (!line_end(lexer)) {
    int32_t c = lexer->lookahead;
    bool candidate = scanner->inline_enabled && !scanner->text_escaped &&
                     inline_before(scanner->text_previous) &&
                     (c == '*' || c == '`' || c == ':' || name_start(c));
    if (candidate && consumed) break;
    if (candidate) {
      // Mark a one-character fallback before looking ahead through the construct.
      lexer->advance(lexer, false);
      lexer->mark_end(lexer);
      if (c == '*' || c == '`') {
        unsigned width = 1;
        if (lexer->lookahead == c) { lexer->advance(lexer, false); width++; }
        enum TokenType token = c == '*' ? (width == 2 ? STRONG_START : EMPHASIS_START) :
                                         (width == 2 ? LITERAL_START : INTERPRETED_START);
        lexer->mark_end(lexer);
        if (scan_inline_pair(scanner, lexer, c, width, token)) return true;
        // Both opening characters are plain on failure.
        text_character(scanner, c);
        lexer->result_symbol = TEXT_FRAGMENT;
        return true;
      }
      if (c == ':') {
        // The first colon is already consumed.
        bool has_name = name_start(lexer->lookahead);
        int32_t previous = 0;
        while (name_continue(lexer->lookahead) || lexer->lookahead == ':' ||
               lexer->lookahead == '-' || lexer->lookahead == '+' || lexer->lookahead == '.') {
          previous = lexer->lookahead;
          lexer->advance(lexer, false);
        }
        if (has_name && previous == ':' && lexer->lookahead == '`') {
          lexer->mark_end(lexer); // Role token excludes the opening backtick.
          lexer->advance(lexer, false);
          if (scan_inline_pair(scanner, lexer, '`', 1, ROLE_START)) return true;
        }
      } else {
        // Simple references and standalone URI schemes share a word prefix.
        char scheme[16] = {0};
        scheme[0] = c < 0x80 ? (char)c : '?';
        uint32_t length = 1;
        unsigned underscores = c == '_' ? 1 : 0;
        while (name_continue(lexer->lookahead) || lexer->lookahead == '-' ||
               (lexer->lookahead == '.' && !underscores)) {
          int32_t current = lexer->lookahead;
          if (length < sizeof(scheme) - 1) scheme[length] = current < 0x80 ? (char)current : '?';
          underscores = current == '_' ? underscores + 1 : 0;
          lexer->advance(lexer, false);
          length++;
          if (!underscores) lexer->mark_end(lexer);
        }
        if (c != '_' && underscores && underscores <= 2 && length > underscores &&
            (inline_after(lexer->lookahead) || lexer->eof(lexer))) {
          scanner->inline_close = (uint8_t)underscores;
          scanner->text_colons = 0;
          lexer->result_symbol = SIMPLE_REFERENCE;
          return true;
        }
        if (lexer->lookahead == ':') {
          lexer->advance(lexer, false);
          bool uri = false;
          if (lexer->lookahead == '/') {
            lexer->advance(lexer, false);
            if (lexer->lookahead == '/') { lexer->advance(lexer, false); uri = true; }
          } else if (strcmp(scheme, "mailto") == 0) { uri = true; }
          if (uri) {
            bool content = false;
            int depth = 0;
            while (!line_end(lexer) && !horizontal_space(lexer->lookahead) &&
                   lexer->lookahead != '<' && lexer->lookahead != '>' && lexer->lookahead != '"' &&
                   lexer->lookahead != '\'') {
              int32_t current = lexer->lookahead;
              if (current == ')' && depth == 0) break;
              if (current == '(') depth++;
              if (current == ')') depth--;
              lexer->advance(lexer, false);
              if (current != '.' && current != ',' && current != ';' && current != '!' && current != '?') {
                lexer->mark_end(lexer);
                content = true;
              }
            }
            if (content) {
              scanner->text_previous = 'x';
              scanner->text_colons = 0;
              lexer->result_symbol = URL;
              return true;
            }
          }
        }
      }
      text_character(scanner, c);
      lexer->result_symbol = TEXT_FRAGMENT;
      return true;
    }
    text_character(scanner, c);
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    consumed = true;
  }
  lexer->result_symbol = TEXT_FRAGMENT;
  return true;
}

void *tree_sitter_docstring_external_scanner_create(void) {
  Scanner *scanner = ts_calloc(1, sizeof(Scanner));
  scanner->pending_indent = UINT32_MAX;
  scanner->literal_indent = UINT32_MAX;
  return scanner;
}

void tree_sitter_docstring_external_scanner_destroy(void *payload) {
  ts_free(payload);
}

unsigned tree_sitter_docstring_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *scanner = payload;
  memcpy(buffer, &scanner->section_indent, 4);
  memcpy(buffer + 4, &scanner->entry_indent, 4);
  memcpy(buffer + 8, &scanner->pending_indent, 4);
  memcpy(buffer + 12, &scanner->literal_indent, 4);
  buffer[16] = (char)scanner->section;
  buffer[17] = (char)scanner->in_entry;
  memcpy(buffer + 18, &scanner->block_indent, 4);
  memcpy(buffer + 22, &scanner->fence_length, 4);
  memcpy(buffer + 26, &scanner->doctest_indent, 4);
  buffer[30] = (char)scanner->block_mode;
  buffer[31] = scanner->fence_character;
  buffer[32] = (char)scanner->doctest_mode;
  buffer[33] = (char)scanner->doctest_code;
  buffer[34] = scanner->entry_name_pending;
  buffer[35] = scanner->in_text;
  buffer[36] = scanner->inline_enabled;
  buffer[37] = scanner->text_line_start;
  buffer[38] = scanner->text_space;
  buffer[39] = scanner->text_escaped;
  memcpy(buffer + 40, &scanner->text_indent, 4);
  memcpy(buffer + 44, &scanner->text_colons, 4);
  memcpy(buffer + 48, &scanner->text_previous, 4);
  memcpy(buffer + 52, &scanner->inline_length, 4);
  buffer[56] = scanner->inline_close;
  return 57;
}

void tree_sitter_docstring_external_scanner_deserialize(void *payload, const char *buffer,
                                                      unsigned length) {
  Scanner *scanner = payload;
  memset(scanner, 0, sizeof(Scanner));
  scanner->pending_indent = UINT32_MAX;
  scanner->literal_indent = UINT32_MAX;
  if (length == 57) {
    memcpy(&scanner->section_indent, buffer, 4);
    memcpy(&scanner->entry_indent, buffer + 4, 4);
    memcpy(&scanner->pending_indent, buffer + 8, 4);
    memcpy(&scanner->literal_indent, buffer + 12, 4);
    scanner->section = (uint8_t)buffer[16];
    scanner->in_entry = buffer[17] != 0;
    memcpy(&scanner->block_indent, buffer + 18, 4);
    memcpy(&scanner->fence_length, buffer + 22, 4);
    memcpy(&scanner->doctest_indent, buffer + 26, 4);
    scanner->block_mode = (uint8_t)buffer[30];
    scanner->fence_character = buffer[31];
    scanner->doctest_mode = (uint8_t)buffer[32];
    scanner->doctest_code = buffer[33] != 0;
    scanner->entry_name_pending = buffer[34] != 0;
    scanner->in_text = buffer[35] != 0;
    scanner->inline_enabled = buffer[36] != 0;
    scanner->text_line_start = buffer[37] != 0;
    scanner->text_space = buffer[38] != 0;
    scanner->text_escaped = buffer[39] != 0;
    memcpy(&scanner->text_indent, buffer + 40, 4);
    memcpy(&scanner->text_colons, buffer + 44, 4);
    memcpy(&scanner->text_previous, buffer + 48, 4);
    memcpy(&scanner->inline_length, buffer + 52, 4);
    scanner->inline_close = (uint8_t)buffer[56];
  }
}

bool tree_sitter_docstring_external_scanner_scan(void *payload, TSLexer *lexer,
                                               const bool *valid_symbols) {
  if (valid_symbols[ERROR_SENTINEL]) return false;

  Scanner *scanner = payload;
  if (scanner->doctest_code && valid_symbols[CODE_LINE]) {
    scanner->doctest_code = false;
    if (lexer->eof(lexer)) return false;
    while (!line_end(lexer)) lexer->advance(lexer, false);
    if (lexer->lookahead == '\r') lexer->advance(lexer, false);
    if (lexer->lookahead == '\n') lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    scanner->pending_indent = UINT32_MAX;
    lexer->result_symbol = CODE_LINE;
    return true;
  }
  if (scanner->block_mode &&
      (valid_symbols[CODE_CONTENT] || valid_symbols[FENCE_END] ||
       valid_symbols[DIRECTIVE_OPTION] || valid_symbols[CODE_END])) {
    return scan_code_block(scanner, lexer, valid_symbols);
  }
  if (scanner->in_text) return scan_text(scanner, lexer, valid_symbols);
  if (scanner->entry_name_pending && (valid_symbols[IDENTIFIER] || valid_symbols[EXCEPTION_NAME])) {
    while (lexer->lookahead == '*') lexer->advance(lexer, false);
    while (name_continue(lexer->lookahead) ||
           (valid_symbols[EXCEPTION_NAME] && lexer->lookahead == '.')) lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    scanner->entry_name_pending = false;
    lexer->result_symbol = valid_symbols[EXCEPTION_NAME] ? EXCEPTION_NAME : IDENTIFIER;
    return true;
  }
  uint32_t column = lexer->get_column(lexer);
  bool line_start = column == 0 || scanner->pending_indent != UINT32_MAX ||
                    lexer->is_at_included_range_start(lexer);
  uint32_t indent = scanner->pending_indent == UINT32_MAX
                        ? column
                        : scanner->pending_indent;
  while (horizontal_space(lexer->lookahead) || lexer->lookahead == '\n' ||
         lexer->lookahead == '\r') {
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
      if (line_start) scanner->doctest_mode = 0;
      indent = 0;
      line_start = true;
      // CRLF is one line ending, not a blank line ending a doctest transcript.
      if (lexer->lookahead == '\r') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '\n') lexer->advance(lexer, true);
        continue;
      }
    } else if (lexer->lookahead == '\t') {
      indent = (indent / 8 + 1) * 8;
    } else {
      indent++;
    }
    lexer->advance(lexer, true);
  }
  lexer->mark_end(lexer);

  if (scanner->doctest_mode && indent < scanner->doctest_indent) scanner->doctest_mode = 0;
  bool in_doctest = scanner->doctest_mode != 0;
  uint32_t literal_indent = scanner->literal_indent;
  bool in_literal = literal_indent != UINT32_MAX && indent > literal_indent;
  if (!in_literal) literal_indent = UINT32_MAX;
  unsigned trailing_colons = 0;
  bool trailing_space = false;

  if (scanner->in_entry && valid_symbols[ENTRY_END] &&
      (lexer->eof(lexer) || (line_start && indent <= scanner->entry_indent))) {
    scanner->in_entry = false;
    scanner->pending_indent = indent;
    scanner->literal_indent = literal_indent;
    lexer->result_symbol = ENTRY_END;
    return true;
  }
  if (scanner->section && !scanner->in_entry && valid_symbols[SECTION_END] &&
      (lexer->eof(lexer) || (line_start && indent <= scanner->section_indent))) {
    scanner->section = 0;
    scanner->pending_indent = indent;
    scanner->literal_indent = literal_indent;
    lexer->result_symbol = SECTION_END;
    return true;
  }
  if (lexer->eof(lexer)) return false;

  if (line_start && !in_literal && !in_doctest && valid_symbols[FENCE_START] &&
      (lexer->lookahead == '`' || lexer->lookahead == '~')) {
    char character = (char)lexer->lookahead;
    unsigned length = 0;
    while (lexer->lookahead == character) {
      lexer->advance(lexer, false);
      length++;
    }
    if (length >= 3) {
      while (!line_end(lexer)) lexer->advance(lexer, false);
      lexer->mark_end(lexer);
      scanner->block_mode = 1;
      scanner->block_indent = indent;
      scanner->fence_character = character;
      scanner->fence_length = length;
      scanner->pending_indent = UINT32_MAX;
      scanner->doctest_mode = 0;
      lexer->result_symbol = FENCE_START;
      return true;
    }
    goto text;
  }
  if (line_start && (lexer->lookahead == '>' || lexer->lookahead == '.')) {
    int32_t character = lexer->lookahead;
    unsigned length = 0;
    while (lexer->lookahead == character) {
      lexer->advance(lexer, false);
      length++;
    }
    if (length == 3 && valid_symbols[DOCTEST_PROMPT] &&
        (character == '>' || (scanner->doctest_mode == 1 && scanner->doctest_indent == indent)) &&
        (horizontal_space(lexer->lookahead) || line_end(lexer))) {
      if (horizontal_space(lexer->lookahead)) lexer->advance(lexer, false);
      lexer->mark_end(lexer);
      scanner->doctest_mode = 1;
      scanner->doctest_code = true;
      scanner->doctest_indent = indent;
      scanner->pending_indent = UINT32_MAX;
      lexer->result_symbol = DOCTEST_PROMPT;
      return true;
    }
    if (character == '.' && length == 2 && !in_literal && !in_doctest &&
        valid_symbols[CODE_DIRECTIVE] && horizontal_space(lexer->lookahead)) {
      skip_horizontal_space(lexer);
      char name[16] = {0};
      unsigned size = 0;
      while (name_continue(lexer->lookahead) || lexer->lookahead == '-') {
        if (size < sizeof(name) - 1) name[size] = (char)lexer->lookahead;
        size++;
        lexer->advance(lexer, false);
      }
      if (size < sizeof(name) &&
          (strcmp(name, "code-block") == 0 || strcmp(name, "sourcecode") == 0) &&
          lexer->lookahead == ':') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == ':') {
          lexer->advance(lexer, false);
          while (!line_end(lexer)) lexer->advance(lexer, false);
          lexer->mark_end(lexer);
          scanner->block_mode = 2;
          scanner->block_indent = indent;
          scanner->pending_indent = UINT32_MAX;
          scanner->doctest_mode = 0;
          lexer->result_symbol = CODE_DIRECTIVE;
          return true;
        }
      }
    }
    goto text;
  }

  if (!in_literal && !in_doctest && valid_symbols[ENTRY_START]) {
    bool is_exception = scanner->section == RAISES_HEADER + 1 || scanner->section == WARNS_HEADER + 1;
    if (!is_exception) {
      for (unsigned stars = 0; stars < 2 && lexer->lookahead == '*'; stars++) lexer->advance(lexer, false);
    }
    bool complete = name_start(lexer->lookahead);
    while (name_continue(lexer->lookahead) || (is_exception && lexer->lookahead == '.')) lexer->advance(lexer, false);
    skip_horizontal_space(lexer);
    if (complete && !is_exception && lexer->lookahead == '(') {
      lexer->advance(lexer, false);
      complete = scan_parenthesized_type(lexer);
      if (complete) { lexer->advance(lexer, false); skip_horizontal_space(lexer); }
    }
    if (complete && lexer->lookahead == ':') {
      lexer->advance(lexer, false);
      if (lexer->lookahead != ':') {
        scanner->in_entry = true;
        scanner->entry_name_pending = true;
        scanner->entry_indent = indent;
        scanner->pending_indent = UINT32_MAX;
        scanner->literal_indent = UINT32_MAX;
        lexer->result_symbol = ENTRY_START;
        return true;
      }
    }
    goto text;
  }

  if (!in_literal && !in_doctest && valid_symbols[PARENTHESIZED_TYPE]) {
    if (lexer->lookahead == ')' || !scan_parenthesized_type(lexer)) return false;
    lexer->mark_end(lexer);
    scanner->pending_indent = UINT32_MAX;
    lexer->result_symbol = PARENTHESIZED_TYPE;
    return true;
  }

  if (!in_literal && !in_doctest && valid_symbols[RETURN_TYPE]) {
    if (scan_return_type(lexer, &trailing_colons)) {
      scanner->pending_indent = UINT32_MAX;
      lexer->result_symbol = RETURN_TYPE;
      return true;
    }
  } else if (!in_literal && !in_doctest && !scanner->in_entry) {
    char heading[32] = {0};
    uint32_t length = 0;
    while (name_continue(lexer->lookahead)) {
      if (length < sizeof(heading) - 1) {
        int32_t c = lexer->lookahead;
        heading[length] = c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') :
                          c < 0x80 ? (char)c : '?';
      }
      length++;
      lexer->advance(lexer, false);
    }
    skip_horizontal_space(lexer);

    // Normalize only the heading probe; the token still preserves its source text.
    while (name_start(lexer->lookahead)) {
      if (length < sizeof(heading) - 1) heading[length] = ' ';
      length++;
      while (name_continue(lexer->lookahead)) {
        int32_t c = lexer->lookahead;
        if (length < sizeof(heading) - 1) {
          heading[length] = c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') :
                            c < 0x80 ? (char)c : '?';
        }
        length++;
        lexer->advance(lexer, false);
      }
      skip_horizontal_space(lexer);
    }
    static const struct { const char *name; enum TokenType token; } headings[] = {
      {"args", ARGS_HEADER},
      {"arguments", ARGS_HEADER},
      {"parameters", ARGS_HEADER},
      {"return", RETURNS_HEADER},
      {"returns", RETURNS_HEADER},
      {"yield", YIELDS_HEADER},
      {"yields", YIELDS_HEADER},
      {"raise", RAISES_HEADER},
      {"raises", RAISES_HEADER},
      {"attributes", ATTRIBUTES_HEADER},
      {"keyword args", KEYWORD_ARGS_HEADER},
      {"keyword arguments", KEYWORD_ARGS_HEADER},
      {"other args", OTHER_PARAMETERS_HEADER},
      {"other arguments", OTHER_PARAMETERS_HEADER},
      {"other parameters", OTHER_PARAMETERS_HEADER},
      {"warn", WARNS_HEADER},
      {"warns", WARNS_HEADER},
      {"example", EXAMPLES_HEADER},
      {"examples", EXAMPLES_HEADER},
      {"note", NOTES_HEADER},
      {"notes", NOTES_HEADER},
      {"warning", WARNING_HEADER},
      {"warnings", WARNING_HEADER},
      {"attention", ADMONITION_HEADER},
      {"caution", ADMONITION_HEADER},
      {"danger", ADMONITION_HEADER},
      {"error", ADMONITION_HEADER},
      {"hint", ADMONITION_HEADER},
      {"important", ADMONITION_HEADER},
      {"tip", ADMONITION_HEADER},
      {"see also", SEE_ALSO_HEADER},
      {"references", REFERENCES_HEADER},
      {"todo", TODO_HEADER},
      {"methods", METHODS_HEADER},
    };
    for (unsigned i = 0; i < sizeof(headings) / sizeof(headings[0]); i++) {
      enum TokenType token = headings[i].token;
      if (line_start && valid_symbols[token] && length < sizeof(heading) &&
          strcmp(heading, headings[i].name) == 0 && lexer->lookahead == ':') {
        lexer->advance(lexer, false);
        trailing_colons = 1;
        trailing_space = horizontal_space(lexer->lookahead);
        lexer->mark_end(lexer);
        skip_horizontal_space(lexer);
        if (line_end(lexer)) {
          scanner->section = (uint8_t)(token + 1);
          scanner->section_indent = indent;
          scanner->pending_indent = UINT32_MAX;
          scanner->literal_indent = UINT32_MAX;
          lexer->result_symbol = (TSSymbol)token;
          return true;
        }
        break;
      }
    }
  }

text:
  if (!valid_symbols[TEXT_START]) return false;
  scanner->in_text = true;
  scanner->inline_enabled = !in_literal && !in_doctest;
  scanner->text_line_start = line_start;
  scanner->text_indent = indent;
  scanner->text_colons = trailing_colons;
  scanner->text_space = trailing_space;
  scanner->text_previous = 0;
  scanner->text_escaped = false;
  scanner->literal_indent = literal_indent;
  if (scanner->doctest_mode) scanner->doctest_mode = 2;
  scanner->pending_indent = UINT32_MAX;
  lexer->result_symbol = TEXT_START;
  return true;
}

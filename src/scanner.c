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
  SECTION_END,
  ENTRY_END,
  IDENTIFIER,
  EXCEPTION_NAME,
  PARENTHESIZED_TYPE,
  RETURN_TYPE,
  TEXT,
  ERROR_SENTINEL,
};

typedef struct {
  uint32_t section_indent;
  uint32_t entry_indent;
  // Layout tokens leave the lexer after indentation, which may contain tabs.
  uint32_t pending_indent;
  uint8_t section;
  bool in_entry;
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

// A return type ends at the first colon outside nested syntax or quoted markup.
static bool scan_return_type(TSLexer *lexer) {
  bool has_content = false;
  if (lexer->lookahead == ':') {
    lexer->advance(lexer, false);
    int32_t previous = 0;
    while (name_continue(lexer->lookahead) || lexer->lookahead == ':' ||
           lexer->lookahead == '-' || lexer->lookahead == '+') {
      previous = lexer->lookahead;
      lexer->advance(lexer, false);
    }
    // A leading colon is type syntax only when it opens a Sphinx role.
    if (previous != ':' || lexer->lookahead != '`') return false;
    has_content = true;
  }

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
    } else if (c == '(' || c == '[' || c == '{') {
      depth++;
    } else if ((c == ')' || c == ']' || c == '}') && depth) {
      depth--;
    } else if (c == ':' && depth == 0) {
      return has_content;
    }
    has_content = has_content || !horizontal_space(c);
    lexer->advance(lexer, false);
  }
  return false;
}

void *tree_sitter_docstring_external_scanner_create(void) {
  Scanner *scanner = ts_calloc(1, sizeof(Scanner));
  scanner->pending_indent = UINT32_MAX;
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
  buffer[12] = (char)scanner->section;
  buffer[13] = (char)scanner->in_entry;
  return 14;
}

void tree_sitter_docstring_external_scanner_deserialize(void *payload, const char *buffer,
                                                      unsigned length) {
  Scanner *scanner = payload;
  memset(scanner, 0, sizeof(Scanner));
  scanner->pending_indent = UINT32_MAX;
  if (length == 14) {
    memcpy(&scanner->section_indent, buffer, 4);
    memcpy(&scanner->entry_indent, buffer + 4, 4);
    memcpy(&scanner->pending_indent, buffer + 8, 4);
    scanner->section = (uint8_t)buffer[12];
    scanner->in_entry = buffer[13] != 0;
  }
}

bool tree_sitter_docstring_external_scanner_scan(void *payload, TSLexer *lexer,
                                               const bool *valid_symbols) {
  if (valid_symbols[ERROR_SENTINEL]) return false;

  Scanner *scanner = payload;
  uint32_t column = lexer->get_column(lexer);
  bool line_start = column == 0 || scanner->pending_indent != UINT32_MAX ||
                    lexer->is_at_included_range_start(lexer);
  uint32_t indent = scanner->pending_indent == UINT32_MAX
                        ? column
                        : scanner->pending_indent;
  while (horizontal_space(lexer->lookahead) || lexer->lookahead == '\n' ||
         lexer->lookahead == '\r') {
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
      indent = 0;
      line_start = true;
    } else if (lexer->lookahead == '\t') {
      indent = (indent / 8 + 1) * 8;
    } else {
      indent++;
    }
    lexer->advance(lexer, true);
  }
  lexer->mark_end(lexer);

  if (scanner->in_entry && valid_symbols[ENTRY_END] &&
      (lexer->eof(lexer) || (line_start && indent <= scanner->entry_indent))) {
    scanner->in_entry = false;
    scanner->pending_indent = indent;
    lexer->result_symbol = ENTRY_END;
    return true;
  }
  if (scanner->section && !scanner->in_entry && valid_symbols[SECTION_END] &&
      (lexer->eof(lexer) || (line_start && indent <= scanner->section_indent))) {
    scanner->section = 0;
    scanner->pending_indent = indent;
    lexer->result_symbol = SECTION_END;
    return true;
  }
  if (lexer->eof(lexer)) return false;

  if (valid_symbols[PARENTHESIZED_TYPE]) {
    if (lexer->lookahead == ')' || !scan_parenthesized_type(lexer)) return false;
    lexer->mark_end(lexer);
    scanner->pending_indent = UINT32_MAX;
    lexer->result_symbol = PARENTHESIZED_TYPE;
    return true;
  }

  if (valid_symbols[RETURN_TYPE]) {
    if (scan_return_type(lexer)) {
      lexer->mark_end(lexer);
      scanner->pending_indent = UINT32_MAX;
      lexer->result_symbol = RETURN_TYPE;
      return true;
    }
  } else if (!scanner->in_entry) {
    char heading[16] = {0};
    uint32_t length = 0;
    bool is_exception = valid_symbols[EXCEPTION_NAME];
    if (!is_exception) {
      for (unsigned stars = 0; stars < 2 && lexer->lookahead == '*'; stars++) {
        lexer->advance(lexer, false);
        length++;
      }
    }
    bool has_name = name_start(lexer->lookahead);
    while (name_continue(lexer->lookahead) || (is_exception && lexer->lookahead == '.')) {
      if (length < sizeof(heading) - 1) {
        heading[length] = lexer->lookahead < 0x80 ? (char)lexer->lookahead : '?';
      }
      length++;
      lexer->advance(lexer, false);
    }
    lexer->mark_end(lexer);
    skip_horizontal_space(lexer);

    static const char *const headings[] = {"Args", "Returns", "Yields", "Raises", "Attributes"};
    for (unsigned token = ARGS_HEADER; token <= ATTRIBUTES_HEADER; token++) {
      if (valid_symbols[token] && length < sizeof(heading) &&
          strcmp(heading, headings[token]) == 0 && lexer->lookahead == ':') {
        lexer->advance(lexer, false);
        lexer->mark_end(lexer);
        skip_horizontal_space(lexer);
        if (line_end(lexer)) {
          scanner->section = (uint8_t)(token + 1);
          scanner->section_indent = indent;
          scanner->pending_indent = UINT32_MAX;
          lexer->result_symbol = (TSSymbol)token;
          return true;
        }
        break;
      }
    }

    if (has_name && (valid_symbols[IDENTIFIER] || is_exception)) {
      bool complete = true;
      if (!is_exception && lexer->lookahead == '(') {
        lexer->advance(lexer, false);
        complete = scan_parenthesized_type(lexer);
        if (complete) {
          lexer->advance(lexer, false);
          skip_horizontal_space(lexer);
        }
      }
      if (complete && lexer->lookahead == ':') {
        scanner->in_entry = true;
        scanner->entry_indent = indent;
        scanner->pending_indent = UINT32_MAX;
        lexer->result_symbol = is_exception ? EXCEPTION_NAME : IDENTIFIER;
        return true;
      }
    }
  }

  if (!valid_symbols[TEXT]) return false;
  while (!line_end(lexer)) {
    lexer->advance(lexer, false);
  }
  lexer->mark_end(lexer);
  scanner->pending_indent = UINT32_MAX;
  lexer->result_symbol = TEXT;
  return true;
}

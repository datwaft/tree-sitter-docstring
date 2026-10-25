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
  TEXT,
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
  return 18;
}

void tree_sitter_docstring_external_scanner_deserialize(void *payload, const char *buffer,
                                                      unsigned length) {
  Scanner *scanner = payload;
  memset(scanner, 0, sizeof(Scanner));
  scanner->pending_indent = UINT32_MAX;
  scanner->literal_indent = UINT32_MAX;
  if (length == 18) {
    memcpy(&scanner->section_indent, buffer, 4);
    memcpy(&scanner->entry_indent, buffer + 4, 4);
    memcpy(&scanner->pending_indent, buffer + 8, 4);
    memcpy(&scanner->literal_indent, buffer + 12, 4);
    scanner->section = (uint8_t)buffer[16];
    scanner->in_entry = buffer[17] != 0;
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

  if (!in_literal && valid_symbols[PARENTHESIZED_TYPE]) {
    if (lexer->lookahead == ')' || !scan_parenthesized_type(lexer)) return false;
    lexer->mark_end(lexer);
    scanner->pending_indent = UINT32_MAX;
    lexer->result_symbol = PARENTHESIZED_TYPE;
    return true;
  }

  if (!in_literal && valid_symbols[RETURN_TYPE]) {
    if (scan_return_type(lexer, &trailing_colons)) {
      scanner->pending_indent = UINT32_MAX;
      lexer->result_symbol = RETURN_TYPE;
      return true;
    }
  } else if (!in_literal && !scanner->in_entry) {
    char heading[32] = {0};
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
        int32_t c = lexer->lookahead;
        heading[length] = c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') :
                          c < 0x80 ? (char)c : '?';
      }
      length++;
      lexer->advance(lexer, false);
    }
    lexer->mark_end(lexer);
    skip_horizontal_space(lexer);

    // Normalize only the heading probe; the token still preserves its source text.
    bool single_word = true;
    while (name_start(lexer->lookahead)) {
      single_word = false;
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
      lexer->mark_end(lexer);
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

    if (has_name && single_word && (valid_symbols[IDENTIFIER] || is_exception)) {
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
        lexer->advance(lexer, false);
        if (lexer->lookahead != ':') {
          scanner->in_entry = true;
          scanner->entry_indent = indent;
          scanner->pending_indent = UINT32_MAX;
          scanner->literal_indent = UINT32_MAX;
          lexer->result_symbol = is_exception ? EXCEPTION_NAME : IDENTIFIER;
          return true;
        }
        trailing_colons = 1;
      }
    }
  }

  if (!valid_symbols[TEXT]) return false;
  while (!line_end(lexer)) {
    if (lexer->lookahead == ':') {
      trailing_colons = trailing_space ? 1 : trailing_colons + 1;
      trailing_space = false;
    } else if (horizontal_space(lexer->lookahead)) {
      trailing_space = true;
    } else {
      trailing_colons = 0;
      trailing_space = false;
    }
    lexer->advance(lexer, false);
  }
  lexer->mark_end(lexer);
  scanner->literal_indent = !in_literal && line_start && trailing_colons >= 2
                                ? indent : literal_indent;
  scanner->pending_indent = UINT32_MAX;
  lexer->result_symbol = TEXT;
  return true;
}

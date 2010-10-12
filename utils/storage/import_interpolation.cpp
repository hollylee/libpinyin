/* 
 *  libpinyin
 *  Library to deal with pinyin.
 *  
 *  Copyright (C) 2010 Peng Wu
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <stdio.h>
#include <glib.h>
#include "novel_types.h"
#include "memory_chunk.h"
#include "phrase_index.h"
#include "ngram.h"
#include "phrase_large_table.h"
#include "tag_utility.h"

enum LINE_TYPE{
    BEGIN_LINE = 1,
    END_LINE,
    GRAM_1_LINE,
    GRAM_2_LINE,
    GRAM_1_ITEM_LINE,
    GRAM_2_ITEM_LINE
};

static int line_type = 0;
static GPtrArray * values = NULL;
static GHashTable * required = NULL;
/* variables for line buffer. */
static char * linebuf = NULL;
static size_t len = 0;

phrase_token_t string_to_token(PhraseLargeTable * phrases,
                               const char * string);

bool parse_unigram(FILE * input, PhraseLargeTable * phrases,
                   FacadePhraseIndex * phrase_index);

bool parse_bigram(FILE * input, PhraseLargeTable * phrases,
                  FacadePhraseIndex * phrase_index,
                  Bigram * bigram);

static ssize_t my_getline(FILE * input){
    ssize_t result = getline(&linebuf, &len, input);
    if ( result == -1 )
        return result;

    linebuf[strlen(linebuf) - 1] = '\0';
    return result;
}

bool parse_body(FILE * input, PhraseLargeTable * phrases,
                FacadePhraseIndex * phrase_index,
                Bigram * bigram){
    taglib_push_state();

    assert(taglib_add_tag(END_LINE, "\\end", 0, "", ""));
    assert(taglib_add_tag(GRAM_1_LINE, "\\1-gram", 0, "", ""));
    assert(taglib_add_tag(GRAM_2_LINE, "\\2-gram", 0, "", ""));

    do {
    retry:
        assert(taglib_read(linebuf, line_type, values, required));
        switch(line_type) {
        case END_LINE:
            goto end;
        case GRAM_1_LINE:
            my_getline(input);
            parse_unigram(input, phrases, phrase_index);
            goto retry;
        case GRAM_2_LINE:
            my_getline(input);
            parse_bigram(input, phrases, phrase_index, bigram);
            goto retry;
        default:
            assert(false);
        }
    } while (my_getline(input) != -1) ;

 end:
    taglib_pop_state();
    return true;
}

bool parse_unigram(FILE * input, PhraseLargeTable * phrases,
                   FacadePhraseIndex * phrase_index){
    taglib_push_state();

    assert(taglib_add_tag(GRAM_1_ITEM_LINE, "\\item", 1, "count", ""));

    do {
        assert(taglib_read(linebuf, line_type, values, required));
        switch(line_type) {
        case GRAM_1_ITEM_LINE:{
            /* handle \item in \1-gram */
            assert(values->len == 1);
            const char * string = (const char *)g_ptr_array_index(values, 0);
            phrase_token_t token = string_to_token(phrases, string);
            char * value = NULL;
            assert(g_hash_table_lookup_extended(required, "count", NULL, (gpointer *)&value));
            glong count = atol(value);
            phrase_index->add_unigram_frequency(token, count);
            break;
        }
        case END_LINE:
        case GRAM_1_LINE:
        case GRAM_2_LINE:
            goto end;
        default:
            assert(false);
        }
    } while (my_getline(input) != -1) ;

 end:
    taglib_pop_state();
    return true;
}

bool parse_bigram(FILE * input, PhraseLargeTable * phrases,
                  FacadePhraseIndex * phrase_index,
                  Bigram * bigram){
    taglib_push_state();

    assert(taglib_add_tag(GRAM_2_ITEM_LINE, "\\item", 2, "count", ""));

    taglib_pop_state();
    return true;
}

int main(int argc, char * argv[]){
    FILE * input = stdin;
    const char * bigram_filename = "../../data/bigram.db";

    PhraseLargeTable phrases;

    MemoryChunk * chunk = new MemoryChunk;
    chunk->load("../../data/phrase_index.bin");
    phrases.load(chunk);

    FacadePhraseIndex phrase_index;

    //gb_char binary file
    chunk = new MemoryChunk;
    chunk->load("../../data/gb_char.bin");
    phrase_index.load(1, chunk);

    //gbk_char binary file
    chunk = new MemoryChunk;
    chunk->load("../../data/gbk_char.bin");
    phrase_index.load(2, chunk);

    Bigram bigram;
    bigram.attach(NULL, bigram_filename);

    taglib_init();

    values = g_ptr_array_new();
    required = g_hash_table_new(g_str_hash, g_str_equal);

    //enter "\data" line
    assert(taglib_add_tag(BEGIN_LINE, "\\data", 0, "model", ""));
    my_getline(input);

    //read "\data" line
    assert(taglib_read(linebuf, line_type, values, required));
    assert(line_type == BEGIN_LINE);
    char * value = NULL;
    assert(g_hash_table_lookup_extended(required, "model", NULL, (gpointer *)&value));
    if ( !( strcmp("interpolation", value) == 0 ) ) {
        fprintf(stderr, "error: interpolation model expected.\n");
        exit(1);
    }

    my_getline(input);
    parse_body(input, &phrases, &phrase_index, &bigram);

    taglib_fini();

    chunk = new MemoryChunk;
    phrase_index.store(1, chunk);
    chunk->save("../../data/gb_char.bin");
    phrase_index.load(1, chunk);

    chunk = new MemoryChunk;
    phrase_index.store(2, chunk);
    chunk->save("../../data/gbk_char.bin");
    phrase_index.load(2, chunk);

    return 0;
}

static phrase_token_t special_string_to_token(const char * string){
    struct token_pair{
        phrase_token_t token;
        const char * string;
    };

    static const token_pair tokens [] = {
        {sentence_start, "<start>"},
        {0, NULL}
    };

    const token_pair * pair = tokens;
    while (pair->string) {
        if ( strcmp(string, pair->string ) == 0 ){
            return pair->token;
        }
    }

    fprintf(stderr, "error: unknown token:%s.\n", string);
    return 0;
}

phrase_token_t string_to_token(PhraseLargeTable * phrases, const char * string){
    phrase_token_t token = 0;
    if ( string[0] == '<' ) {
        return special_string_to_token(string);
    }

    glong phrase_len = g_utf8_strlen(string, -1);
    utf16_t * phrase = g_utf8_to_utf16(string, -1, NULL, NULL, NULL);
    int result = phrases->search(phrase_len, phrase, token);
    if ( !(result & SEARCH_OK) )
        fprintf(stderr, "error: unknown token:%s.\n", string);

    g_free(phrase);
    return token;
}
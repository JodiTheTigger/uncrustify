/**
 * @file parens.cpp
 * Adds or removes parens.
 *
 * @author  Ben Gardner
 * @license GPL v2+
 */

#include "parens.h"

#include "log_rules.h"

using namespace uncrustify;


//! Add an open parenthesis after first and add a close parenthesis before the last
static void add_parens_between(chunk_t *first, chunk_t *last);


/**
 * Scans between two parens and adds additional parens if needed.
 * This function is recursive. If it hits another open paren, it'll call itself
 * with the new bounds.
 *
 * Adds optional parens in an IF or SWITCH conditional statement.
 *
 * This basically just checks for a CT_COMPARE that isn't surrounded by parens.
 * The edges for the compare are the open, close and any CT_BOOL tokens.
 *
 * This only handles VERY simple patterns:
 *   (!a && b)         => (!a && b)          -- no change
 *   (a && b == 1)     => (a && (b == 1))
 *   (a == 1 || b > 2) => ((a == 1) || (b > 2))
 *
 * FIXME: we really should bail if we transition between a preprocessor and
 *        a non-preprocessor
 */
static void check_bool_parens(chunk_t *popen, chunk_t *pclose, int nest);


void do_parens(void)
{
   constexpr static auto LCURRENT = LPARADD;

   LOG_FUNC_ENTRY();

   log_rule_B("mod_full_paren_if_bool");

   if (options::mod_full_paren_if_bool())
   {
      chunk_t *pc = chunk_get_head();

      while ((pc = chunk_get_next_nc_nnl(pc)) != nullptr)
      {
         if (  pc->type != CT_SPAREN_OPEN
            || (  get_chunk_parent_type(pc) != CT_IF
               && get_chunk_parent_type(pc) != CT_ELSEIF
               && get_chunk_parent_type(pc) != CT_SWITCH))
         {
            continue;
         }
         // Grab the close sparen
         chunk_t *pclose = chunk_get_next_type(pc, CT_SPAREN_CLOSE, pc->level, scope_e::PREPROC);

         if (pclose != nullptr)
         {
            check_bool_parens(pc, pclose, 0);
            pc = pclose;
         }
      }
   }
}


static void add_parens_between(chunk_t *first, chunk_t *last)
{
   LOG_FUNC_ENTRY();

   LOG_FMT(LPARADD, "%s: line %zu between %s [lvl=%zu] and %s [lvl=%zu]\n",
           __func__, first->orig_line,
           first->text(), first->level,
           last->text(), last->level);

   // Don't do anything if we have a bad sequence, ie "&& )"
   chunk_t *first_n = chunk_get_next_nc_nnl(first);

   if (first_n == last)
   {
      return;
   }
   chunk_t pc;

   set_chunk_type(&pc, CT_PAREN_OPEN);
   pc.orig_line   = first_n->orig_line;
   pc.orig_col    = first_n->orig_col;
   pc.str         = "(";
   pc.flags       = first_n->flags & PCF_COPY_FLAGS;
   pc.level       = first_n->level;
   pc.pp_level    = first_n->pp_level;
   pc.brace_level = first_n->brace_level;

   chunk_add_before(&pc, first_n);

   chunk_t *last_p = chunk_get_prev_nc_nnl(last, scope_e::PREPROC);

   set_chunk_type(&pc, CT_PAREN_CLOSE);
   pc.orig_line   = last_p->orig_line;
   pc.orig_col    = last_p->orig_col;
   pc.str         = ")";
   pc.flags       = last_p->flags & PCF_COPY_FLAGS;
   pc.level       = last_p->level;
   pc.pp_level    = last_p->pp_level;
   pc.brace_level = last_p->brace_level;

   chunk_add_after(&pc, last_p);

   for (chunk_t *tmp = first_n;
        tmp != last_p;
        tmp = chunk_get_next_nc_nnl(tmp))
   {
      tmp->level++;
   }

   last_p->level++;
} // add_parens_between


static void check_bool_parens(chunk_t *popen, chunk_t *pclose, int nest)
{
   LOG_FUNC_ENTRY();

   chunk_t *ref        = popen;
   bool    hit_compare = false;

   LOG_FMT(LPARADD, "%s(%d): popen on %zu, col %zu, pclose on %zu, col %zu, level=%zu\n",
           __func__, nest,
           popen->orig_line, popen->orig_col,
           pclose->orig_line, pclose->orig_col,
           popen->level);

   chunk_t *pc = popen;

   while (  (pc = chunk_get_next_nc_nnl(pc)) != nullptr
         && pc != pclose)
   {
      if (pc->flags.test(PCF_IN_PREPROC))
      {
         LOG_FMT(LPARADD2, " -- bail on PP %s [%s] at line %zu col %zu, level %zu\n",
                 get_token_name(pc->type),
                 pc->text(), pc->orig_line, pc->orig_col, pc->level);
         return;
      }

      if (  chunk_is_token(pc, CT_BOOL)
         || chunk_is_token(pc, CT_QUESTION)
         || chunk_is_token(pc, CT_COND_COLON)
         || chunk_is_token(pc, CT_COMMA))
      {
         LOG_FMT(LPARADD2, " -- %s [%s] at line %zu col %zu, level %zu\n",
                 get_token_name(pc->type),
                 pc->text(), pc->orig_line, pc->orig_col, pc->level);

         if (hit_compare)
         {
            hit_compare = false;

            if (!language_is_set(LANG_CS))
            {
               add_parens_between(ref, pc);
            }
         }
         ref = pc;
      }
      else if (chunk_is_token(pc, CT_COMPARE))
      {
         LOG_FMT(LPARADD2, " -- compare [%s] at line %zu col %zu, level %zu\n",
                 pc->text(), pc->orig_line, pc->orig_col, pc->level);
         hit_compare = true;
      }
      else if (chunk_is_paren_open(pc))
      {
         chunk_t *next = chunk_skip_to_match(pc);

         if (next != nullptr)
         {
            check_bool_parens(pc, next, nest + 1);
            pc = next;
         }
      }
      else if (  chunk_is_token(pc, CT_BRACE_OPEN)
              || chunk_is_token(pc, CT_SQUARE_OPEN)
              || chunk_is_token(pc, CT_ANGLE_OPEN))
      {
         // Skip [], {}, and <>
         pc = chunk_skip_to_match(pc);
      }
   }

   if (  hit_compare
      && ref != popen
      && !language_is_set(LANG_CS))
   {
      add_parens_between(ref, pclose);
   }
} // check_bool_parens

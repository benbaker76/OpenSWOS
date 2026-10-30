/* The Mod's gp_season (tools/gplay.c, included as it is) over one season's tables. */
#include <stdio.h>
#include <stdlib.h>
#include GPLAY_C
const u8 SEC_TAB[1] = { 0 };            /* (no second teams: play-outs take them anyway) */
s8 SEC_SLT[4096], SEC_COPY[4096];
const u8 GP_PROM_TAB[1] = { 0 };
u16 GP_TOPW[9];
u8 GP_TOPR[9 * 684];
const u8 GP_HLT[4] = { 0 };
void mod_stages(const unsigned short (*r)[32][6], const int *n, int nk, const unsigned short (*keep)[6], int fresh)
{
    for (int k = 0; k < 4; k++) {
        GP_SSB[0].n[k] = (u8)n[k];
        for (int i = 0; i < n[k]; i++) {
            struct gp_row *x = &GP_SSB[0].r[k][i];
            x->team = r[k][i][0]; x->glob = r[k][i][1]; x->won = r[k][i][2];
            x->gf = r[k][i][3]; x->ga = r[k][i][4]; x->pts = r[k][i][5];
        }
    }
    GP_SSB[0].fresh = (u8)fresh;
    GP_IN.nst[0] = (u8)nk;
    GP_IN.nst[1] = 0;
    for (int i = 0; i < nk; i++) {
        struct gp_row *x = &GP_IN.st[0][i];
        x->team = keep[i][0]; x->glob = keep[i][1]; x->won = 0; x->gf = 0; x->ga = 0; x->pts = keep[i][5];
    }
}
void mod_gp(const unsigned short (*rows)[32][6], const int *n, int nd, const u8 *sp, FILE *out)
{
    GP_IN.nd = (u8)nd;
    for (int d = 0; d < nd; d++) {
        GP_IN.n[d] = (u8)n[d];
        for (int k = 0; k < n[d]; k++) {
            struct gp_row *r = &GP_IN.t[d][k];
            r->team = rows[d][k][0]; r->glob = rows[d][k][1]; r->won = rows[d][k][2];
            r->gf = rows[d][k][3]; r->ga = rows[d][k][4]; r->pts = rows[d][k][5];
        }
    }
    GP_IN.spec = sp;
    gp_season();
    for (int i = 0; i < GP_OUT.nmv; i++) fprintf(out, " g%u:%d", GP_OUT.mv[i].glob, GP_OUT.mv[i].delta);
    for (int i = 0; i < GP_OUT.nres; i++)
        fprintf(out, " T%u:%u/%u", GP_OUT.res[i].kind, GP_OUT.res[i].glob[0], GP_OUT.res[i].glob[1]);
    for (int k = 0; k < 3; k++)
        if (GP_OUT.sres[k].kind) fprintf(out, " S%d:%u/%u", k, GP_OUT.sres[k].glob[0], GP_OUT.sres[k].glob[1]);
}
/* The career club's play-off at its league's end (gp_need, a career), for every club of every
   division: its index and the divisions simulated early. */
void mod_need(FILE *out)
{
    GP_IN.gtype = 4;
    for (int d = 0; d < GP_IN.nd; d++)
        for (int k = 0; k < GP_IN.n[d]; k++) {
            GP_IN.mdiv = (u8)d;
            GP_IN.mgr = GP_IN.t[d][k].team;
            if (!gp_need()) continue;
            fprintf(out, " N%d.%d:%d", d, k, GP_IN.pi[0]);
            if (!t2_shape(po_at(GP_IN.pi[0]), 0))
                for (int j = 0; j < SD && GP_IN.sim[j] != NOC; j++) fprintf(out, "/%d", GP_IN.sim[j]);
        }
}

/* The career club's contest (ko_build) for each play-off of knockouts, the club the first entrant
   that waits in round 1: its round-1 ties simulated at the build (presim) -- their winners -- and,
   by record ('host_by_record'), its clubs in rank order. */
static u8 KO_NM[12 * 64];
void mod_ko(FILE *out)
{
    const u8 *s;
    GP_NM = (const u32 *)KO_NM;
    GP_RN = 0;
    GP_TH = 0;
    GP_IN.gtype = 4;
    for (int pi = 0; (s = po_at(pi)); pi++) {
        int n = s[1], grp = 0, wait = -1, in0[64] = { 0 }, ok = 1;
        const u8 *r = s + 6 + 4 * n, *r0 = r;
        u16 team[64];
        for (int k = 0; k < s[2]; k++) { grp |= r[1] & 0x80; r += 6 + 2 * r[0]; }
        {                                       /* (entrants of more than three divisions: not ours) */
            int dv[64], nd = 0;
            for (int i = 0; i < n; i++) {
                int j = 0;
                while (j < nd && dv[j] != s[7 + 4 * i]) j++;
                if (j == nd) dv[nd++] = s[7 + 4 * i];
            }
            grp |= nd > 3;
        }
        if (grp || r0[4] || n > 64) continue;   /* (group rounds, a paired round 1: not here) */
        for (int i = 0; i < n; i++) {
            int c = ent_club(s + 6 + 4 * i, s[0]);
            if (c < 0) ok = 0; else team[i] = row(c)->team;
        }
        if (!ok) continue;
        for (int i = 0; i < r0[0]; i++)
            for (int x = 0; x < 2; x++) if (r0[6 + 2 * i + x] < n) in0[r0[6 + 2 * i + x]] = 1;
        for (int i = 0; i < n && wait < 0; i++) {
            if (in0[i]) continue;
            wait = i;
            for (int j = 0; j < n; j++) if (j != i && team[j] == team[i]) wait = -1;
        }
        if (wait < 0) continue;
        GP_IN.mgr = team[wait];
        struct gp_ko *k = &GP_ST.ko[0];
        ko_build(k, pi, 4, 0xFE);
        fprintf(out, " K%d:", pi);
        for (int t = 0; t < r0[0]; t++) fprintf(out, "%04X,", k->tw[t] != NOC ? k->team[k->tw[t]] : 0);
        if (s[5] & 2)
            for (int p = 0; p < 256; p++)
                for (int j = 0; j < k->n; j++) if (k->prio[j] == p) fprintf(out, "r%04X", k->team[j]);
    }
}

/* the contests' checks leave the season's state as they found it (gp_need picks a bank, ko_build
   marks GP_ST): the next season's gp_season must not see them */
static struct gp_state ko_st;
static struct gp_in ko_in;
static unsigned char ko_ssb[sizeof GP_SSB];
void mod_ko_all(FILE *out)
{
    struct gp_ss *ssb = SSB;
    ko_st = GP_ST; ko_in = GP_IN;
    for (unsigned i = 0; i < sizeof GP_SSB; i++) ko_ssb[i] = ((unsigned char *)GP_SSB)[i];
    mod_ko(out);
    mod_need(out);
    GP_ST = ko_st; GP_IN = ko_in; SSB = ssb;
    for (unsigned i = 0; i < sizeof GP_SSB; i++) ((unsigned char *)GP_SSB)[i] = ko_ssb[i];
}

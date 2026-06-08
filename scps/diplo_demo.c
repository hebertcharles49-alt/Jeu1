/*
 * diplo_demo.c — banc d'essai diplomatie & guerre (§5-§6)
 *
 *   make diplo_demo && ./diplo_demo [graine]
 *
 * Prouve :
 *   1. Les relations se LISENT : menace, parenté (sphère), schisme, alliance —
 *      calculées sur des coordonnées existantes, pas posées à la main.
 *   2. La guerre = territoire CONTRE diversité : conquérir une culture lointaine
 *      monte le D̄ interne du conquérant → la fracture monte (gated par K).
 *      Concorde « Unie » → « Murmurante/Fracturée ».
 */
#include "scps_world.h"
#include "scps_econ.h"
#include "scps_trade.h"
#include "scps_tech.h"
#include "scps_legitimacy.h"
#include "scps_prosperity.h"
#include "scps_readout.h"
#include "scps_diplo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass=0,g_fail=0;
static void ok(const char*w,bool c){ printf("   %s %s\n",c?"✓":"✗",w); if(c)g_pass++; else g_fail++; }

static float content_dist(const PopCulture*a,const PopCulture*b){
    float dv=a->valeurs-b->valeurs;       if(dv<0)dv=-dv;
    float ds=a->subsistance-b->subsistance; if(ds<0)ds=-ds;
    float dp=a->parente-b->parente;       if(dp<0)dp=-dp;
    float dr=a->religion-b->religion;     if(dr<0)dr=-dr;
    float m=dv; if(ds>m)m=ds; if(dp>m)m=dp; if(dr>m)m=dr; return m;
}

int main(int argc,char**argv){
    uint32_t seed=(argc>1)?(uint32_t)strtoul(argv[1],NULL,10):42u;
    World*w=malloc(sizeof(World)); WorldEconomy*econ=malloc(sizeof(WorldEconomy));
    TradeNetwork*net=malloc(sizeof(TradeNetwork)); TechState*ts=calloc(SCPS_MAX_COUNTRY,sizeof(TechState));
    WorldProsperity*wp=malloc(sizeof(WorldProsperity)); WorldLegitimacy*wl=malloc(sizeof(WorldLegitimacy));
    DiploState*dp=malloc(sizeof(DiploState));
    if(!w||!econ||!net||!ts||!wp||!wl||!dp){fprintf(stderr,"OOM\n");return 1;}

    printf("══════════════════════════════════════════════════════════════\n");
    printf(" DIPLOMATIE & GUERRE — lire les relations, payer la conquête (graine %u)\n",seed);
    printf("══════════════════════════════════════════════════════════════\n");

    WorldParams p=worldparams_default(seed);
    world_generate(w,&p);
    econ_init(econ,w); gen_population(w,econ); worldgen_seed_peoples(w,econ,RACE_HUMAIN);
    trade_network_build(net,w,econ);
    for(int c=0;c<w->n_countries;c++) tech_state_init(&ts[c],false);
    prosperity_init(wp,w); legitimacy_init(wl,w,econ); diplo_init(dp);

    int player=0; for(int c=0;c<w->n_countries;c++) if(w->country[c].role==POLITY_PLAYER){player=c;break;}

    /* L'économie se met en route, SANS colonisation : le joueur reste sur sa
     * seule capitale (homogène, D̄≈0) — pour isoler l'effet de la conquête. */
    for(int t=0;t<8;t++){
        econ_tick(econ, 1.f);
        legitimacy_tick(wl,w,econ,ts); prosperity_tick(wp,w,econ,net,ts,wl);
    }

    /* ---- 1. Les relations se lisent ---------------------------------- */
    printf("\n── 1. Relations du joueur (pays %d) — tout est LU ──\n",player);
    printf("   %-10s %-8s %-8s %-8s %-8s %-9s\n","pays","menace","parenté","schisme","compl.","alliance");
    int shown=0;
    for(int c=0;c<w->n_countries && shown<6;c++){
        if(c==player) continue;
        Relation r=diplo_relation(w,econ,wp,dp,player,c);
        printf("   #%-9d %-8.2f %-8.0f %-8.2f %-8.2f %-+9.2f\n",
               c,r.threat,r.kinship,r.schism,r.complement,r.alliance);
        shown++;
    }

    /* ---- 2. La guerre monte la diversité ----------------------------- */
    printf("\n── 2. Territoire CONTRE diversité ──\n");
    const PopCulture *pcap=NULL;
    { int cp=w->country[player].capital_prov;
      if(cp>=0){int cr=w->province[cp].region; if(cr>=0)pcap=&econ->region[cr].culture;} }

    /* cible : la région PEUPLÉE la plus LOINTAINE culturellement, hors joueur. */
    int target=-1; float best=-1.f; int tgt_owner=-1;
    for(int r=0;r<econ->n_regions;r++){
        RegionEconomy*re=&econ->region[r];
        if(!re->culture.settled || re->owner==player || re->owner<0) continue;
        float d = pcap?content_dist(&re->culture,pcap):0.f;
        if(d>best){best=d; target=r; tgt_owner=re->owner;}
    }
    if(target<0){ printf("   (pas de cible peuplée ennemie — monde trop vide)\n"); }
    else {
        CountryProsperity*cp=&wp->country[player];
        float Dbar0=cp->profile.D_bar_int, frac0=cp->fracture;
        CountryReadout r0=country_readout(wp,ts,w,player);
        printf("   AVANT : Concorde=%-12s  [dev D̄_int=%.2f fracture=%.2f]\n",
               label_concorde(r0.concorde), Dbar0, frac0);

        printf("   → guerre au pays #%d, conquête de la région %d (« %s », D∞=%.1f de nous)\n",
               tgt_owner, target, w->region[target].name, best);
        diplo_declare_war(dp,player,tgt_owner);
        bool took=diplo_conquer_region(dp,w,econ,wl,player,target,false);

        /* recalcul : la région conquise est désormais à nous → diversité. */
        for(int t=0;t<3;t++){ legitimacy_tick(wl,w,econ,ts); prosperity_tick(wp,w,econ,net,ts,wl); }
        float Dbar1=cp->profile.D_bar_int, frac1=cp->fracture;
        CountryReadout r1=country_readout(wp,ts,w,player);
        printf("   APRÈS : Concorde=%-12s  [dev D̄_int=%.2f fracture=%.2f]\n",
               label_concorde(r1.concorde), Dbar1, frac1);

        printf("\n── Vérification ──\n");
        ok("la conquête a eu lieu (owner transféré)", took && econ->region[target].owner==player);
        ok("la région conquise démarre à légitimité effondrée",
           wl->L[target] <= 2.0f);
        ok("conquérir une culture lointaine monte la diversité interne (D̄↑)",
           Dbar1 > Dbar0 + 0.3f);
        ok("la diversité non métabolisée monte la fracture", frac1 > frac0);
    }

    /* ---- 3. Diplomatie d'ÉQUILIBRE : trêve · momentum · friction · coalition ---- */
    printf("\n── 3. Diplomatie d'équilibre (rétroaction négative, pas d'interdit) ──\n");
    {
        /* B = un voisin ; C = un pays avec une FORCE réelle (l'allié dont l'entrée
         * pèse) — sinon le coût d'élargissement serait nul (pays vide). */
        int A=player, B=-1, C=-1;
        for(int c=0;c<w->n_countries;c++){ if(c==A||w->country[c].role==POLITY_UNCLAIMED) continue;
            if(B<0){B=c;continue;}
            if(diplo_mil_power(w,econ,c)>0.01f){ C=c; break; } }
        if(B>=0 && C>=0){
            /* TRÊVE : une longue guerre → une longue trêve ; on n'enchaîne plus. */
            diplo_init(dp); dp->war_years[A][B]=dp->war_years[B][A]=4.f;
            diplo_make_peace(dp,A,B);
            float tr4=diplo_truce_days(dp,A,B);
            ok("après la paix, on ne peut PAS redéclarer (trêve)", !diplo_can_declare(dp,A,B));
            diplo_init(dp); dp->war_years[A][B]=dp->war_years[B][A]=8.f;
            diplo_make_peace(dp,A,B);
            ok("une plus LONGUE guerre → une plus longue trêve", diplo_truce_days(dp,A,B) > tr4);
            diplo_tick(dp, 365.f*15.f);
            ok("la trêve FOND : la guerre redevient possible après le répit", diplo_can_declare(dp,A,B));

            /* MOMENTUM : la fulgurance effraie plus que la masse statique. */
            diplo_init(dp);
            float th0=diplo_relation(w,econ,wp,dp,A,B).threat;
            dp->momentum[B]=6.f;
            float th1=diplo_relation(w,econ,wp,dp,A,B).threat;
            ok("un conquérant FULGURANT menace plus qu'à puissance statique égale", th1 > th0+0.01f);

            /* FRICTION : un protégé d'un allié puissant renchérit (coût d'élargissement). */
            diplo_init(dp);
            float wno=diplo_war_widening_cost(w,econ,dp,A,B);
            diplo_form_alliance(dp,B,C);
            ok("frapper un protégé d'allié ÉLARGIT la guerre (coût ↑)",
               diplo_war_widening_cost(w,econ,dp,A,B) > wno);

            /* COALITION : un hégémon (fulgurance extrême) est perçu comme menace dominante. */
            diplo_init(dp); dp->momentum[B]=40.f;
            ok("un hégémon fulgurant est PERÇU (posture de coalition, sans script)",
               diplo_perceived_hegemon(w,econ,wp,dp,A)==B);
        } else {
            ok("(monde trop petit pour le test d'équilibre)", true);
        }
    }

    /* ---- 4. Casus belli : la guerre a une RAISON (lue) ; son type gate la paix ---- */
    printf("\n── 4. Casus belli (la guerre a une raison ; son type fixe le but) ──\n");
    {
        int A=player, B=-1, Badj=-1;
        for(int c=0;c<w->n_countries;c++){
            if(c==A||w->country[c].role==POLITY_UNCLAIMED) continue;
            if(B<0)B=c;
            bool adj=false;
            for(int r=0;r<econ->n_regions&&!adj;r++) if(econ->region[r].owner==A)
                for(int s=0;s<econ->n_regions;s++) if(econ->region[s].owner==c&&econ->adj[r][s]){adj=true;break;}
            if(adj&&Badj<0)Badj=c;
        }
        if(B>=0){
            diplo_init(dp); diplo_declare_war_cb(dp,A,B,CB_ECONOMIC);
            ok("déclarer avec un casus belli MÉMORISE le but de guerre", diplo_war_goal(dp,A,B)==CB_ECONOMIC);
            diplo_make_peace(dp,A,B);
            ok("la paix ÉTEINT le but de guerre", diplo_war_goal(dp,A,B)==CB_NONE);
            int Br=-1; for(int r=0;r<econ->n_regions;r++) if(econ->region[r].owner==B){Br=r;break;}
            if(Br>=0){
                for(int r=0;r<econ->n_regions;r++) if(econ->region[r].owner==A) econ->region[r].raw_cap[RES_SALTPETER]=0.f;
                econ->region[Br].raw_cap[RES_SALTPETER]=2.f;   /* B monopolise un bien stratégique */
                ok("un bien aigu MONOPOLISÉ par la cible → casus belli ÉCONOMIQUE",
                   diplo_casus_belli(w,econ,wp,dp,A,B,RES_SALTPETER)==CB_ECONOMIC);
            }
        }
        if(Badj>=0)
            ok("un voisin (adjacence) fournit un casus belli — une raison existe (≠ aucun)",
               diplo_casus_belli(w,econ,wp,dp,A,Badj,RES_NONE)!=CB_NONE);
    }

    /* ---- 4b. CROISADE faustienne : Gardiens orthodoxes vs Transgresseurs ---- */
    printf("\n── 4b. La croisade faustienne (l'orthodoxe frappe le développeur de l'interdit) ──\n");
    {
        int crusader=player, heretic=-1;
        for(int c=0;c<w->n_countries;c++) if(c!=crusader && w->country[c].role!=POLITY_UNCLAIMED){ heretic=c; break; }
        if(heretic>=0){
            diplo_init(dp);
            int capR=w->province[w->country[crusader].capital_prov].region;
            econ->region[capR].culture.ethos=ETHOS_ORDRE;          /* le croisé est ORTHODOXE */
            diplo_set_faustian(dp,heretic,6.0f);                    /* l'hérétique développe l'interdit */
            ok("la souillure faustienne se LIT", diplo_faustian(dp,heretic)==6.0f);
            ok("un orthodoxe a un CASUS BELLI (croisade) contre un développeur faustien",
               diplo_faustian_cb(w,econ,dp,crusader,heretic) &&
               diplo_casus_belli(w,econ,wp,dp,crusader,heretic,RES_NONE)==CB_RELIGIOUS);
            diplo_set_faustian(dp,heretic,0.f);
            ok("sans souillure faustienne, PAS de croisade", !diplo_faustian_cb(w,econ,dp,crusader,heretic));
            econ->region[capR].culture.ethos=ETHOS_DOMINATEUR;     /* un empire permissif */
            diplo_set_faustian(dp,heretic,6.0f);
            ok("un empire PERMISSIF ne croise pas (il cède lui-même à l'interdit)",
               !diplo_faustian_cb(w,econ,dp,crusader,heretic));
        } else ok("(monde trop petit pour la croisade)", true);
    }

    /* ---- 5. Score de guerre (§2) : le bras-de-fer (batailles, occupation, attrition) ---- */
    printf("\n── 5. Score de guerre (batailles plafonnées +50 · occupation · attrition) ──\n");
    {
        int A=player, B=-1, Ar=-1;
        for(int c=0;c<w->n_countries && B<0;c++){
            if(c==A||w->country[c].role==POLITY_UNCLAIMED) continue;
            for(int r=0;r<econ->n_regions;r++) if(econ->region[r].owner==c){ B=c; break; }
        }
        for(int r=0;r<econ->n_regions;r++) if(econ->region[r].owner==A){ Ar=r; break; }
        if(B>=0 && Ar>=0){
            diplo_init(dp);
            diplo_declare_war_cb(dp,A,B,CB_TERRITORIAL);   /* A = attaquant */
            float armsA0=0.f;
            for(int r=0;r<econ->n_regions;r++) if(econ->region[r].owner==A){
                econ->region[r].stock[RES_ENCHANTED_ARMS]=1000.f; armsA0+=1000.f; }   /* A surarme */
            for(int y=0;y<15;y++) diplo_war_tick(dp,w,econ,wp,1.f);
            ok("l'avantage militaire POUSSE le battle_score (l'attaquant gagne les batailles)",
               dp->battle_score[A][B] > 5.f);
            ok("le battle_score est PLAFONNÉ à +50 (les batailles seules ne gagnent pas)",
               dp->battle_score[A][B] <= 50.01f);
            dp->conquered[A][B]=5;
            ok("l'occupation porte le score AU-DELÀ de +50 (l'autre moitié)",
               diplo_war_score(dp,A,B) > 50.5f);
            float armsA1=0.f;
            for(int r=0;r<econ->n_regions;r++) if(econ->region[r].owner==A) armsA1+=econ->region[r].stock[RES_ENCHANTED_ARMS];
            ok("l'attrition SAIGNE les armes pendant la guerre (épuisement)", armsA1 < armsA0);
        }
    }

    /* ---- 6. Saccage (§4) : la prise DÉPOUILLE la province (1×/5 ans) ---- */
    printf("\n── 6. Saccage (or + production → trésor de l'occupant · 1×/5 ans) ──\n");
    if(econ->n_regions>=2){
        int vic=0, occ=1;
        RegionEconomy *rv=&econ->region[vic], *ro=&econ->region[occ];
        rv->pillage_cd=0.f; rv->revolt_scar=0.f; rv->treasury=1000.f;
        for(int g=1;g<RES_COUNT;g++){ rv->stock[g]=10.f; rv->price[g]=1.f; }
        float occ0=ro->treasury, vic0=rv->treasury;
        float loot=diplo_pillage_region(econ,vic,occ);
        printf("   sac de la région %d → %d : butin = %.0f or-équiv.\n",vic,occ,loot);
        ok("le saccage rapporte un butin (or des coffres + entrepôt valorisé)", loot>600.f);
        ok("le trésor de l'OCCUPANT enfle du butin entier",
           ro->treasury > occ0+loot-1.f && ro->treasury < occ0+loot+1.f);
        ok("la province pillée est VIDÉE de son or", rv->treasury < vic0);
        ok("le sac CONVULSE la province (cicatrice au plancher → gel)", rv->revolt_scar > 0.99f);
        float loot2=diplo_pillage_region(econ,vic,occ);
        ok("on ne RE-saccage pas avant 5 ans (plus rien à prendre)", loot2==0.f);
        ok("le compteur anti-saccage est armé (~5 ans)", rv->pillage_cd > 4.5f);
        for(int t=0;t<6;t++) econ_tick(econ,1.f);   /* 6 ans s'écoulent (cd décroît par an) */
        ok("après ~5 ans, la province REDEVIENT saccageable", rv->pillage_cd <= 0.f);
    } else ok("(monde trop petit pour le test de saccage)", true);

    /* ---- 7. Paix proportionnelle (§5) : revendication ∝ domination · surexpansion → coalition ---- */
    printf("\n── 7. Paix proportionnelle (revendication ∝ domination · indemnité · surexpansion) ──\n");
    {
        /* B = le pays NON-joueur le plus ÉTOFFÉ (≥2 régions) : prendre UNE province ne
         * l'anéantit pas → sa puissance reste lisible (claim stable) et il reste un
         * trésor à ponctionner après. */
        int A=player, B=-1, Bn=0;
        for(int c=0;c<w->n_countries;c++){
            if(c==A||w->country[c].role==POLITY_UNCLAIMED) continue;
            int n=0; for(int r=0;r<econ->n_regions;r++) if(econ->region[r].owner==c) n++;
            if(n>Bn){ Bn=n; B=c; }
        }
        if(B>=0 && Bn>=2){
            /* REVENDICATION ∝ domination militaire : un dominant annexe LÉGITIMEMENT plus. */
            for(int r=0;r<econ->n_regions;r++){
                econ->region[r].stock[RES_ARMS]=econ->region[r].stock[RES_GUNPOWDER]=econ->region[r].stock[RES_ENCHANTED_ARMS]=0.f;
                if(econ->region[r].owner==B) econ->region[r].stock[RES_ENCHANTED_ARMS]=4000.f; /* B surarme → A marginal */
            }
            diplo_init(dp); diplo_declare_war_cb(dp,A,B,CB_TERRITORIAL);
            int claim_marg=diplo_war_claim(dp,w,econ,A,B);
            for(int r=0;r<econ->n_regions;r++){
                econ->region[r].stock[RES_ENCHANTED_ARMS]=0.f;
                if(econ->region[r].owner==A) econ->region[r].stock[RES_ENCHANTED_ARMS]=4000.f; /* A surarme → A dominant */
            }
            int claim_dom=diplo_war_claim(dp,w,econ,A,B);
            printf("   revendication territoriale : marginal=%d prov · dominant=%d prov\n",claim_marg,claim_dom);
            ok("un attaquant DOMINANT a une revendication légitime plus large", claim_dom > claim_marg);
            ok("un attaquant marginal n'a droit qu'à la province-frontière (≥1)", claim_marg >= 1);

            /* un casus belli NON-territorial ne donne droit qu'à UNE prise (humiliation/source). */
            diplo_init(dp); diplo_declare_war_cb(dp,A,B,CB_RELIGIOUS);
            ok("un casus belli non-territorial ne vaut qu'UNE prise (pas d'annexion étendue)",
               diplo_war_claim(dp,w,econ,A,B)==1);

            /* SUREXPANSION : prendre AU-DELÀ de la revendication = surcroît de fulgurance.
             * On garde B FORT (surarmé, plusieurs régions) → revendication=1 STABLE même
             * après une prise → la 2e prise est ILLÉGITIME. */
            for(int r=0;r<econ->n_regions;r++){
                econ->region[r].stock[RES_ARMS]=econ->region[r].stock[RES_GUNPOWDER]=0.f;
                econ->region[r].stock[RES_ENCHANTED_ARMS]=(econ->region[r].owner==B)?6000.f:0.f;
            }
            diplo_init(dp); diplo_declare_war_cb(dp,A,B,CB_TERRITORIAL);
            int claim=diplo_war_claim(dp,w,econ,A,B);
            dp->conquered[A][B]=claim;                   /* pile à la limite légitime */
            int Br=-1; for(int r=0;r<econ->n_regions;r++) if(econ->region[r].owner==B && econ->region[r].culture.settled){Br=r;break;}
            if(Br>=0 && claim==1){
                float mom0=dp->momentum[A];
                diplo_conquer_region(dp,w,econ,wl,A,Br,false);  /* la prise (claim+1) = ILLÉGITIME */
                ok("prendre AU-DELÀ de la revendication = SUREXPANSION (fulgurance ↑↑ → coalition)",
                   dp->momentum[A] > mom0 + 2.0f);        /* base 1 + surcharge 2 → > 2 prouve la surcharge */
            } else ok("(pas de cible nette pour le test de surexpansion)", true);

            /* RÉPARATIONS : le vaincu net indemnise le vainqueur ∝ score. */
            diplo_init(dp); diplo_declare_war_cb(dp,A,B,CB_TERRITORIAL);
            dp->battle_score[A][B]=50.f; dp->conquered[A][B]=5;          /* score de A ≈ +100 */
            for(int r=0;r<econ->n_regions;r++) if(econ->region[r].owner==B) econ->region[r].treasury=1000.f;
            int capA=w->province[w->country[A].capital_prov].region;
            float capA0=(capA>=0)?econ->region[capA].treasury:0.f;
            float rep=diplo_reparations(dp,w,econ,A,B);
            printf("   indemnité de guerre extorquée : %.0f or\n",rep);
            ok("le VAINCU net paie une indemnité au vainqueur (∝ score)", rep>0.f);
            ok("le trésor du VAINQUEUR enfle de l'indemnité", capA>=0 && econ->region[capA].treasury > capA0);

            /* match nul : aucune indemnité (pas de vainqueur net). */
            diplo_init(dp);
            ok("un MATCH NUL (score sous le seuil) n'extorque aucune indemnité",
               diplo_reparations(dp,w,econ,A,B)==0.f);
        } else ok("(monde trop petit pour le test de paix proportionnelle)", true);
    }

    /* ---- 8. Rancune nationale (§6) : irrédentisme · ralliement · l'oubli ---- */
    printf("\n── 8. Rancune nationale (irrédentisme · ralliement · l'oubli) ──\n");
    {
        int A=player, P=-1, B=-1, Bn=0;
        for(int c=0;c<w->n_countries;c++){
            if(c==A||w->country[c].role==POLITY_UNCLAIMED) continue;
            if(P<0) P=c;
            int n=0; for(int r=0;r<econ->n_regions;r++) if(econ->region[r].owner==c && econ->region[r].culture.settled) n++;
            if(n>Bn){ Bn=n; B=c; }
        }
        if(P<0){ ok("(monde trop petit pour le test de rancune)", true); }
        else {
            /* RALLIEMENT (avant toute conquête : B intact) — à rancune ÉGALE de
             * domination, le lésé qui reprend ses terres accumule le score plus vite. */
            if(B>=0){
                for(int r=0;r<econ->n_regions;r++){
                    econ->region[r].stock[RES_ARMS]=econ->region[r].stock[RES_GUNPOWDER]=0.f;
                    econ->region[r].stock[RES_ENCHANTED_ARMS]=(econ->region[r].owner==A)?2000.f:0.f;
                }
                diplo_init(dp); diplo_declare_war_cb(dp,A,B,CB_TERRITORIAL);
                for(int y=0;y<3;y++) diplo_war_tick(dp,w,econ,wp,1.f);
                float noRally=dp->battle_score[A][B];
                diplo_init(dp); diplo_declare_war_cb(dp,A,B,CB_TERRITORIAL);
                dp->rancor[A][B]=3.0f;                       /* A reprend SES terres */
                for(int y=0;y<3;y++) diplo_war_tick(dp,w,econ,wp,1.f);
                ok("le RALLIEMENT galvanise la reconquête (bras-de-fer plus rapide à domination égale)",
                   dp->battle_score[A][B] > noRally + 0.5f);
            } else ok("(pas de cible pour le ralliement)", true);

            /* IRRÉDENTISME : la rancune seule donne un CB territorial SANS adjacence. */
            int Bfar=-1;
            for(int c=0;c<w->n_countries;c++){
                if(c==A||w->country[c].role==POLITY_UNCLAIMED) continue;
                bool adj=false;
                for(int r=0;r<econ->n_regions&&!adj;r++) if(econ->region[r].owner==A)
                    for(int s=0;s<econ->n_regions;s++) if(econ->region[s].owner==c&&econ->adj[r][s]){adj=true;break;}
                if(!adj){ Bfar=c; break; }
            }
            if(Bfar>=0){
                diplo_init(dp);
                CasusBelli cb0=diplo_casus_belli(w,econ,wp,dp,A,Bfar,RES_NONE);
                dp->rancor[A][Bfar]=2.0f;
                CasusBelli cb1=diplo_casus_belli(w,econ,wp,dp,A,Bfar,RES_NONE);
                ok("sans rancune NI adjacence : pas de casus belli territorial", cb0!=CB_TERRITORIAL);
                ok("la RANCUNE seule donne un CB territorial (irrédentisme, sans adjacence)", cb1==CB_TERRITORIAL);
            } else ok("(pas de pays non-adjacent pour le test d'irrédentisme)", true);

            /* La rancune SURVIT à la paix, puis S'OUBLIE sur une génération. */
            diplo_init(dp); dp->rancor[A][P]=2.0f;
            diplo_make_peace(dp,A,P);
            ok("la rancune SURVIT à la paix (le grief ne s'éteint pas avec la guerre)", diplo_rancor(dp,A,P) > 1.9f);
            diplo_tick(dp, 365.f*5.f);
            float r5=diplo_rancor(dp,A,P);
            ok("la rancune S'ESTOMPE avec le temps (5 ans → elle fond)", r5 < 1.9f && r5 > 0.f);
            diplo_tick(dp, 365.f*30.f);
            ok("sur une génération, la rancune est OUBLIÉE (→ 0)", diplo_rancor(dp,A,P)==0.f);

            /* CONQUÊTE (en dernier : mute B) — la prise POSE la rancune ; une prise
             * ILLÉGITIME la CREUSE. B surarmé → revendication=1 stable. */
            if(B>=0 && Bn>=2){
                int Br1=-1,Br2=-1;
                for(int r=0;r<econ->n_regions;r++) if(econ->region[r].owner==B && econ->region[r].culture.settled){ if(Br1<0)Br1=r; else {Br2=r;break;} }
                /* perte LÉGITIME (dans la revendication) : la rancune = la seule perte. */
                diplo_init(dp); diplo_declare_war_cb(dp,A,B,CB_TERRITORIAL);
                diplo_conquer_region(dp,w,econ,wl,A,Br1,false);
                float rancor_loss=diplo_rancor(dp,B,A);
                /* perte ILLÉGITIME (occupation forcée BIEN au-delà du légitime) : rancune CREUSÉE. */
                diplo_init(dp); diplo_declare_war_cb(dp,A,B,CB_TERRITORIAL);
                dp->conquered[A][B]=20;                          /* surexpansion manifeste */
                diplo_conquer_region(dp,w,econ,wl,A,Br2,false);
                float rancor_illegit=diplo_rancor(dp,B,A);
                ok("perdre une province POSE la rancune sur le dépossédé", rancor_loss >= 0.9f);
                ok("une prise ILLÉGITIME CREUSE la rancune (perte + agression nue)",
                   rancor_illegit >= rancor_loss + 0.9f);
            } else ok("(pas assez de provinces pour la rancune de conquête)", true);
        }
    }

    /* ---- 9. Esclavage (§4c) : déportation → groupe restif au cœur (D̄↑) ---- */
    printf("\n── 9. Esclavage (déportation → groupe restif au cœur · D̄↑) ──\n");
    {
        int A=player;
        int capR=(w->country[A].capital_prov>=0)? w->province[w->country[A].capital_prov].region : -1;
        int srcR=-1; for(int r=0;r<econ->n_regions;r++) if(r!=capR){ srcR=r; break; }
        if(capR>=0 && srcR>=0){
            /* Le banc tourne en mono-groupe : on PEUPLE explicitement le cœur (natifs)
             * et une province source (un groupe ÉTRANGER — des orques). */
            PopGroup nat; memset(&nat,0,sizeof nat);
            nat.race=RACE_HUMAIN; nat.klass=CLASS_BOURGEOIS; nat.count=8000;
            nat.integration=1.f; nat.L=6.f; nat.drift_id=111; nat.origin_sphere=species_sphere(RACE_HUMAIN);
            econ->region[capR].pop.n_groups=1; econ->region[capR].pop.groups[0]=nat;
            PopGroup foe; memset(&foe,0,sizeof foe);
            foe.race=RACE_ORQUE; foe.klass=CLASS_LABORER; foe.count=4000;
            foe.integration=1.f; foe.L=5.f; foe.drift_id=222; foe.origin_sphere=species_sphere(RACE_ORQUE);
            econ->region[srcR].pop.n_groups=1; econ->region[srcR].pop.groups[0]=foe;

            /* GATE = la TECH d'asservissement (TECH_ESCLAVAGE, signature Orque) : booléen. */
            long captives=diplo_enslave_capture(w,econ,A,srcR,/*enslaves*/true);
            printf("   captifs déportés au cœur : %ld (sur 4000)\n",captives);
            ok("un empire doté de l'Économie servile DÉPORTE ≈¼ de la population prise",
               captives>0 && captives<=1100);
            ok("la capitale gagne un GROUPE de plus — les captifs au cœur",
               econ->region[capR].pop.n_groups==2);
            PopGroup *g=&econ->region[capR].pop.groups[econ->region[capR].pop.n_groups-1];
            ok("le groupe d'esclaves est RESTIF (non-intégré + diaspora → D̄↑ au centre)",
               g->integration<0.01f && g->diaspora && g->race==RACE_ORQUE);
            ok("la province prise PERD la population déportée",
               econ->region[srcR].pop.groups[0].count < 4000);
            /* GATE : sans la TECH d'asservissement (enslaves=false), personne n'est asservi. */
            ok("sans la TECH d'asservissement (TECH_ESCLAVAGE), personne n'est capturé",
               diplo_enslave_capture(w,econ,A,srcR,/*enslaves*/false)==0);
        } else ok("(monde trop petit pour le test d'esclavage)", true);
    }

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf(" BILAN : %d réussis, %d échoués\n",g_pass,g_fail);
    printf("══════════════════════════════════════════════════════════════\n");
    free(w);free(econ);free(net);free(ts);free(wp);free(wl);free(dp);
    return g_fail?1:0;
}

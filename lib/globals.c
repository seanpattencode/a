/* globals — paths:
   AROOT=adata/        DDIR=adata/local/ (gitignored)   SROOT=adata/git/ (synced repo)
   SROOT subdirs: notes/ tasks/ projects/ activity/ sessions/ perf/ workspace/ secrets/
   SROOT/secrets/ — user-managed secrets (recovery keys, tokens, …). Plaintext OK since
   adata/git is a private remote; append your own files there as desired. */
static char HOME[P], TMP[P], DDIR[P], AROOT[P], SROOT[P], SDIR[P], DEV[128], LOGDIR[P];
static struct timespec _t0; static double _rms_v;   /* main() stamps _t0; each menu prints its own render time so a perf regression shows on sight */
static double _rms(void){ if(_rms_v<=0){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);_rms_v=(t.tv_sec-_t0.tv_sec)*1e3+(t.tv_nsec-_t0.tv_nsec)/1e6;} return _rms_v; }

typedef struct { char path[512], repo[512], gdrive[512], name[128], file[P]; int order; } proj_t;
static proj_t PJ[MP]; static int NPJ;

typedef struct { char name[128], cmd[512]; } app_t;
static app_t AP[MA]; static int NAP;

typedef struct { char key[16], name[64], cmd[1024]; } sess_t;
static sess_t SE[MS]; static int NSE;

typedef struct { char k[64], v[1024]; } cfg_t;
static cfg_t CF[64]; static int NCF;

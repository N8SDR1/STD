/* MSHV DecoderMs, JT65 only.
 * Copyright 2015 Hrisimir Hristov, LZ2HV
 * May be used under the terms of the GNU General Public License (GPL)
 *
 * Slim stand-in for decoderms.h. decoderjt65.cpp includes that name,
 * and this header is forced in first so the full DecoderMs (meteor,
 * PI4, every other mode) is not compiled into STD. The methods below
 * are the ones decoderjt65.cpp actually defines.
 */
#ifndef DECODERMS_H
#define DECODERMS_H

// Qt headers before complex.h. complex.h's I macro is the imaginary
// unit the JT65 decoder uses, and it also breaks Qt templates if a
// Qt header is parsed after it.
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QObject>
#include <QString>
#include <QStringList>

#include "HvMsPlayer/libsound/HvGen65/gen65.h"
#include "decoderpom.h"

#ifndef STD_JT65_MOC
#include "decoderq65.h"
#else
class DecoderQ65;
#endif

// AvgDecodeChanged calls this. The real DecoderFt2 already lives in the
// FT8 translation unit. This declaration only has to match the mangled
// non-virtual method. It is never constructed here.
class DecoderFt2 {
public:
  void AvgDecodeChanged(bool);
};

class DecoderMs : public QObject {
  Q_OBJECT
public:
  explicit DecoderMs(QString);
  ~DecoderMs();

  void Set65DeepSearchDb(QStringList);
  void AvgDecodeChanged(bool);
  void DeepSearchChanged(bool);
  void SetClearAvg65();
  void SetMaxCandidats65(int);
  void SetVhfUhfFeatures(bool);
  void SetAggresLevFtd(int);
  void SetAggresLevDeepS(int);
  void prepare65(double nfqso, int ntol, QString time);
  void jt65_decode(double* dd, int npts, int mode65);

signals:
  void EmitDecodetText(QStringList, bool, bool);
  void EmitDecodetTextRxFreq(QStringList, bool, bool);
  void EmitBackColor(bool);
  void EmitAvgSaves(int, int, int, int);

private:
  void SetBackColor();
  void zero_int_beg_end(int* d, int begin, int end);
  void hint65(double s3_[63][64], int nadd, int nflip, QString mycall, QString hiscall,
              QString hisgrid, double& qual, QString& decoded);
  void subtract65(double* dd, int npts, double f0, double dt);
  void getpp_(int* workdat, float& p);
  void ftrsd2_ap(int* mrsym, int* mrprob, int* mr2sym, int* mr2prob, bool f_ap_d, int* ap,
                 int ntrials0, int* correct, int* param, int* ntry);
  void chkhist(int* mrsym, int& nmax, int& ipk);
  void demod64a(double s3_[63][64], int nadd, int* mrsym, int* mrprob, int* mr2sym, int* mr2prob,
                int& ntest, int& nlow);
  void extract(double s3_[63][64], int nadd, int mode65, int ntrials, int naggressive,
               bool f_deep_search, int nflip, QString mycall_12, QString hiscall_12, QString hisgrid,
               int& ncount, int& nhist, QString& decoded, bool& ltext, int& nft, double& qual,
               int nQSOProgress, bool ljt65apon);
  void decode65b(double s2_[126][66], int nflip, int nadd, int mode65, int ntrials, int naggressive,
                 bool f_deep_search, QString mycall, QString hiscall, QString hisgrid, int nqd, int& nft,
                 double& qual, int& nhist, QString& decoded, int nQSOProgress, bool ljt65apon);
  void smo121(double* x, int beg, int nz);
  void twkfreq65(double complex* c4aa, int n5, double* a);
  void ccf2(double* ss, int nz, int nflip, double& ccfbest, double& xlagpk);
  double fchisq65(double complex* cx, int npts, double fsample, int nflip, double* a, double& ccfmax,
                  double& dtmax);
  void afc65b(double complex* cx, int npts, double fsample, int nflip, double* a, double& ccfbest,
              double& dtbest);
  void fil6521(double complex* c1, int n1, double complex* c2, int& n2);
  void sh65snr(double* x, int beg, int nz, double& snr);
  void sh65(double complex* cx, int n5, int mode65, int ntol, double& xdf, int& nspecial, double& snrdb,
            double& nstest);
  void filbig(double* dd, int npts, double f0, bool& newdat, double complex* c4a, int& n4, double& sq0);
  void decode65a(double* dd, int npts, bool& first_time, int nqd, double freq, int& nflip, int mode65,
                 int nvec, int naggressive, bool f_deep_search, int ntol, QString mycall, QString hiscall,
                 QString hisgrid, bool bVHF, double& sync2, double* a, double& dtx, int& nft, int& nspecial,
                 double& qual, int nsmo, QString& decoded, double& nstestn, int nQSOProgress, bool ljt65apon);
  void slope(double* y, int beg, int npts, double xpk);
  void xcor(double ss_[3413][642], int ipk, int nsteps, int nsym, int lag1, int lag2, double* ccf,
            double& ccf0, int& lagpk, double& flip, double fdot, int nrobust);
  double fchisq0(double* y, int npts, double* a);
  void lorentzian(double* y, int npts, double* a);
  void flat65(double ss_[3413][642], int nhsym, int NSZ, double* ref);
  void symspec65(double* dd, int npts, double ss_[3413][642], int& nhsym, double* savg);
  void avg65(int nutc, int& nsave, double snrsync, double dtxx, int nflip, int nfreq, int mode65, int ntol,
             bool f_deep_search, bool nagain, int ntrials, int naggressive, int neme, QString mycall,
             QString hiscall, QString hisgrid, int& nftt, QString& avemsg, double& qave, QString& deepave,
             int& nsum, int ndeepave, int nQSOProgress, bool ljt65apon);

  F2a f2a;
  PomAll pomAll;
  double DEC_SAMPLE_RATE = 12000.0;
  double twopi = 0.0;
  double pi = 0.0;
  int G_MinSigdB = 1;
  int G_DfTolerance = 400;
  QString s_MyCall;
  QString s_MyBaseCall;
  QString s_HisCall;
  bool f_back_color = false;
  int s_decoder_deep = 1;
  bool s_fopen = false;

  int s_aggres_lev_ftd = 0;
  int s_aggres_lev_deeps = 0;
  QStringList db_call_loc4_list;
  double s_nfqso_all = 1270.46;
  bool clearave_jt65 = false;
  bool s_avg_jt65 = false;
  bool s_deep_search_jt65 = false;
  Gen65* TGen65 = nullptr;
  double width_jt65 = 0.0;
  int nsave_jt65 = 0;
  bool first_symspec65 = true;
  double thresh0_jt65 = 1.0;
  double ref_jt65[3413];
  double dfref_jt65 = 0.0;
  double w_symspec65[8192];
  double s1_jt65_[126][512 + 40];
  int s1_ofs_jt65 = 256 + 20;
  double s3a_jt65_[63][64];
  int mrs_jt65[63];
  int mrs2_jt65[63];
  int param_jt65[10];
  int nsum_jt65 = 0;
  int s_max65_cand_for_dec = 8;

  bool first_avg65 = true;
  int iutc_jt65[64];
  int nfsave_jt65[64];
  double dtdiff_jt65 = 0.0;
  double s1save_jt65_[64][126][512 + 40];
  double s3save_jt65_[64][63][64];
  double syncsave_jt65[64];
  double dtsave_jt65[64];
  int nflipsave_jt65[64];
  char cused_jt65[64];
  int iused_jt65[64];
  char csync_jt65 = 0;
  int count_saved_avgs_1jt65 = 0;
  int count_saved_avgs_2jt65 = 0;
  int count_use_avgs_1jt65 = 0;
  int count_use_avgs_2jt65 = 0;

  typedef struct {
    double freq;
    double dt;
    double sync;
    double flip;
  } candidate_jt65;

  void fqso_first(double nfqso, int ntol, candidate_jt65* ca, int ncand);
  void sync65(double ss_[3413][642], double nfa, double nfb, int nqsym, candidate_jt65* ca, int& ncand,
              int nrobust, bool bVHF);
  void sync65_single(double ss_[3413][642], double nfa, double nfb, int nqsym, double& dtx, double& dfx,
                     double& snrx, double& snrsync, double& flip, double* savg, int mode65, int nrobust);

  double complex cw_jt65[721000];
  bool first_subtract65 = true;
  int correct_jt65[63];
  bool s_bVHF_jt65 = false;
  QString mycall0_jt65ap;
  QString hiscall0_jt65ap;
  QString hisgrid0_jt65ap;
  int apsymbols_jt65[8][14];

  bool s_lapon = false;
  int s_nQSOProgress = 0;
  QString HisGridLoc;

  DecoderQ65* DecQ65 = nullptr;
  DecoderFt2* DecFt2_0 = nullptr;

  int s_mousebutton = 0;
  QString s_time;
};

#ifdef STD_JT65_MOC
#undef I
#undef complex
#endif
#endif

/* MSHV DecoderMs, MSK144 and MSK40 only.
 * Copyright 2015 Hrisimir Hristov, LZ2HV
 * May be used under the terms of the GNU General Public License (GPL)
 *
 * Slim stand-in for decoderms.h, forced in ahead of decodermsk144.cpp
 * and decodermsk40.cpp. The class is DecoderMsk so it does not collide
 * with the JT65 DecoderMs in the same DLL. The DecoderMs define below
 * renames the qualified definitions in those two files. The members are
 * the ones the two files use. The algorithms stay in the MSHV tree.
 */
#ifndef DECODERMS_H
#define DECODERMS_H

// Qt headers before complex.h: its I macro breaks Qt templates.
#include <QObject>
#include <QString>
#include <QStringList>

#include <math.h>

#include "HvMsPlayer/libsound/HvGenMsk/genmesage_msk.h"
#include "decoderpom.h"

#define RECENT_CALLS_COU 6
#define HASH_CALLS_COUNT 36
#define RECENT_SHMSGS_COU 50
#define MAX_DISP_DEC_COU 40
#define MAX_RTD_DISP_DEC_COU 140

// SetCalsHash also hands the calls to the FT8, FT4 and Q65 decoders.
// This DecoderMsk owns none of them; these do nothing.
struct StdMskNoDecoder {
  void SetStHisCallGrid(QString, QString, QString) {}
  void SetStHisCallGrid(QString, QString) {}
  void SetStHisCall(QString) {}
};

class DecoderMsk : public QObject {
  Q_OBJECT
public:
  explicit DecoderMsk(QString);
  ~DecoderMsk();

  // STD entry points (msk_bridge.cpp).
  void prepareMsk(int ntol, int depth, bool shortMsgs, bool swl, const QString& time, const QString& myCall,
                  const QString& hisCall);
  void decodeFull(double* dat, int npts, bool mskms);
  void decodeRtd(double* dat, int npts, double istart, bool mskms);
  void endRtd();

  void SetMsk144RxEqual(int);
  void SetShOpt(bool f);
  void SetSwlOpt(bool f);
  void SetCalsHash(QStringList);
  void SetCalsHashFileOpen(QString);

signals:
  void EmitDecodetText(QStringList, bool, bool);
  void EmitDecodetTextRxFreq(QStringList, bool, bool);
  void EmitDecLinesPosToDisplay(int count, double pos, double pos_ping, QString p_time);
  void EmitBackColor(bool);

private:
  F2a f2a;
  PomAll pomAll;
  double DEC_SAMPLE_RATE;
  double twopi;
  double pi;
  int s_mode;
  int G_DfTolerance;
  bool G_ShOpt;
  bool G_SwlOpt;
  bool s_fopen;
  int s_decoder_deep;
  int s_mousebutton;
  QString s_time;
  int rtd_dupe_cou;
  double rtd_dupe_pos;
  QString s_MyCall;
  QString s_MyBaseCall;
  QString s_HisCall;
  QString s_HisBaseCall;
  QString HisGridLoc;
  QString s_R1HisCall;
  QString s_R2HisCall;
  bool f_back_color;
  StdMskNoDecoder noDecoder_;
  StdMskNoDecoder* DecFt8_0 = &noDecoder_;
  StdMskNoDecoder* DecFt4_0 = &noDecoder_;
  StdMskNoDecoder* DecQ65 = &noDecoder_;

  void SetBackColor();
  QString RemBegEndWSpaces(QString);
  QString RemWSpacesInside(QString);
  double dot_product_da_da(double* a, double* b, int count, int beg);
  void EndRtdPeriod();
  void tweak1(double complex* ca, int jz, double f0, double complex* cb);
  void zero_int_beg_end(int*, int begin, int end);

  GenMsk* TGenMsk;
  /// MSK144
  bool first_dec_msk;
  char s_msk144_2s8[8];
  double pp_msk144[12];
  double rcw_msk144[12];
  double complex cb_msk144[42];
  bool f_first_msk144;
  double dt_msk144;
  double fs_msk144;
  void first_msk144();
  int ihlo_msk144;
  int ihhi_msk144;
  int illo_msk144;
  int ilhi_msk144;
  int i2000_msk144;
  int i4000_msk144;
  int last_ntol_msk144;
  double last_df_msk144;
  void dftool_msk144(int ntol, double nrxfreq, double fd);
  void cshift2(double complex* a, double complex* b, int cou, int ish);
  void mplay_dca_dca_dca(double complex* a, int a_beg, int a_end, double complex* b, int b_beg,
                         double complex* mp, int mp_b, int ord);
  void mplay_dca_dca_da(double complex* a, int a_beg, int a_end, double complex* b, int b_beg, double* mp,
                        int mp_b, int ord);
  void mplay_da_da_i(double* a, int b_a, int e_a, double* b, int b_b, int mp);
  void mplay_da_absdca_absdca(double* a, int a_c, double complex* b, double complex* mp);
  void sum_dca_dca_dca(double complex* a, int a_cou, double complex* b, double complex* c);
  void copy_double_ar_ainc(double* a, int a_beg, int a_inc, double* b, int b_beg, int b_end);
  void copy_dca_or_sum_max3dca(double complex* a, int a_cou, double complex* b, int b_beg,
                               double complex* c = 0, int c_beg = -1, double complex* d = 0, int d_beg = -1);
  double sum_da(double* a, int a_beg, int a_end);
  int sum_ia(int* a, int a_beg, int a_end);
  int maxloc_absdca_beg_to_end(double complex* a, int a_beg, int a_end);
  int maxloc_da_end_to_beg(double* a, int a_beg, int a_end);
  QString extractmessage144(char* decoded, int& nhashflag, char& ident);
  void msk144decodeframe_p(double complex* c, double* softbits, QString& msgreceived, int& nsuccess,
                           char& ident, double phase0);
  void msk144decodeframe(double complex* c, double* softbits, QString& msg, int& nsuccess, char& ident,
                         bool f_phase);
  bool s_f_rtd;
  bool s_end_rtd;
  double tsec0_rtd_msk;
  bool first_rtd_msk;
  double pnoise_rtd_msk;
  QString s_time_last;
  int s_nsnrlast;
  QString s_msglast;
  int s_nsnrlastswl;
  QString s_msglastswl;
  double complex dot_product_dca_dca(double complex* a, int b_a, double complex* b, int b_b, int count);
  double complex dot_product_dca_sum_dca_dca(double complex* a, int a_b, int b_b, double complex* c,
                                             int c_count);
  void msk144sync(double complex* cdat, int nframes, int ntol, double delf, int* navmask, int npeaks,
                  double fc, double& fest, int* npklocs, int& nsuccess, double complex* c);
  void msk144spd(double complex* cdat, int np, int& nsuccess, QString& msgreceived, double fc, double& fest,
                 double& tdec, char& ident, int& navg, double complex* ct, double* softbits);
  int navg_sq;
  double complex cross_avg_sq[864];
  double wt_avg_sq;
  double tlast_sq;
  QString trained_dxcall_sq;
  QString training_dxcall_sq;
  bool currently_training_sq;
  bool first_sq;
  void msk144signalquality(double complex* cframe, double snr, double freq, double t0, double* softbits,
                           QString msg, QString dxcall, int& nbiterrors, double& eyeopening, bool& trained,
                           double* pcoeffs, bool f_calc_pcoeffs);
  double s_pcoeffs_msk144[3];
  bool s_trained_msk144;
  QStringList s_list_rpt_msk;
  double prev_pt_msk;
  double ping_width_msk;
  double prev_ping_t_msk;
  double last_rpt_snr_msk;
  bool one_end_ping_msk;
  bool is_new_rpt_msk;
  bool ss_msk144ms;
  QString GetStandardRPT(double width, double peak);
  void print_rtd_decode_text(QString msg, QString& smsg, int in_snr, int& s_snr, double ts, double fest,
                             double t0, int navg, int ncorrected, double eyeopening, char ident);
  QString str_round_20ms(double v);
  void msk_144_40_rtd(double* d2, int n, double s_istart, bool);
  void msk144_freq_search(double complex* cdat, double fc, int if1, int if2, double delf, int nframes,
                          int* navmask, double& xmax, double& bestf, double complex* cs, double* xccs);
  double MskPingDuration(double* detmet_dur, int istp_real, int il, double level, int nstepsize, int nspm,
                         double dt);
  void SetDecodetTextMsk2DL(QStringList);
  void detectmsk144(double complex* cdat, int npts, double s_istart, int& nmessages);
  void opdetmsk144(double complex* cdat, int npts, double s_istart, int& nmessages);
  bool s_msk144rxequal_s;
  bool s_msk144rxequal_d;
  double complex h_msk144_2[524500];
  double complex s_corrs[524500];
  double complex s_corrd[524500];
  int nfft0_msk144_2;
  double dpclast_msk144_2[3];
  bool any_not_and_save_in_a(double* a, double* b, int c);
  void analytic_msk144_2_init_s_corrs_full();
  void analytic_msk144_2(double* d, int d_count_begin, int npts, int nfft, double complex* c, double* dpc,
                         bool bseq, bool bdeq);
  void msk_144_40_decode(double* dat, int npts_in, double s_istart, bool);
  /// MSK40
  typedef struct {
    int hash;
    QString calls;
  } call_;
  call_ hash_msk40_calls[HASH_CALLS_COUNT];
  QString recent_calls[RECENT_CALLS_COU];
  void update_recent_calls(QString call);
  bool check_hash_msk40(int hash_in, int rpt, QString& msg);
  void hash_msk40_all_calls(int id, QString calls);
  bool check_hash_msk40_swl(int hash_in, int rpt, QString& msg);
  void hash_msk40_swl();
  QString recent_shmsgs[RECENT_SHMSGS_COU];
  bool update_recent_shmsgs(QString message);
  char s_msk40_2s8r[8];
  double pp_msk40[12];
  double rcw_msk40[12];
  double complex cbr_msk40[42];
  bool f_first_msk40;
  double dt_msk40;
  double fs_msk40;
  void first_msk40();
  int ihlo_msk40;
  int ihhi_msk40;
  int illo_msk40;
  int ilhi_msk40;
  int i2000_msk40;
  int i4000_msk40;
  int last_ntol_msk40;
  double last_df_msk40;
  void dftool_msk40(int ntol, double nrxfreq, double df);
  void detectmsk40(double complex* cdat, int npts, double s_istart);
  void msk40decodeframe_p(double complex* c, double* softbits, double xsnr, QString& msgreceived,
                          int& nsuccess, char& ident, double phase0);
  void msk40decodeframe(double complex* ct, double* softbits, double xsnr, QString& msg, int& nsuccess,
                        char& ident, bool f_phase);
  void msk40sync(double complex* cdat, int nframes, int ntol, double delf, int* navmask, int npeaks,
                 double fc, double& fest, int* npklocs, int& nsuccess, double complex* c);
  void msk40spd(double complex* cdat, int np, int& nsuccess, QString& msgreceived, double fc, double& fest,
                double& tdec, char& ident, int& navg, double complex* ct, double* softbits);
  void msk40_freq_search(double complex* cdat, double fc, int if1, int if2, double delf, int nframes,
                         int* navmask, double& xmax, double& bestf, double complex* cs, double* xccs);
};

#ifdef STD_MSK_MOC
#undef I
#undef complex
#else
#define DecoderMs DecoderMsk
#endif
#endif

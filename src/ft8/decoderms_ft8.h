/* MSHV DecoderMs
 * Copyright 2015 Hrisimir Hristov, LZ2HV
 * May be used under the terms of the GNU General Public License (GPL)
 */
#ifndef DECODERMS_H
#define DECODERMS_H

#include <complex.h>
#define complex		_Complex
//#include "../Hv_Lib_fftw/fftw3.h"

#include "decoderpom.h"

#include "HvMsPlayer/libsound/HvGenFt8/gen_ft8.h"
#include "HvMsPlayer/libsound/HvGenFt4/gen_ft4.h"
#include "HvMsPlayer/libsound/HvGenFt2/gen_ft2.h"
// FT8 only: gen_sfox.h is not part of this decoder.
#include <QObject> //2.53
#include <QStringList>

#define ALL_MSG_SNR 200  //2.70 from 120
#define MAXDEC 200 		 //2.70 from 120
#define MAXDUPMSGTHR 240 //2.70 from 120 to 250

//rand
//#define	MAX_TYPES0	5
#define	TYPE_00		0
//#define	BREAK_00	8
//#define	DEG_00		0
//#define	SEP_00		0

//#define	TYPE_01		1
//#define	BREAK_01	32
//#define	DEG_01		7
//#define	SEP_01		3

//#define	TYPE_02		2
//#define	BREAK_02	64
//#define	DEG_02		15
//#define	SEP_02		1

//#define	TYPE_03		3
//#define	BREAK_03	128
//#define	DEG_03		31
//#define	SEP_03		3

#define	TYPE_03		3
#define	BREAK_03	128
#define	DEG_03		31
#define	SEP_03		1

//#define	TYPE_04		4
//#define	BREAK_04	256
//#define	DEG_04		63
//#define	SEP_04		1
//end rand

#define numdeccq 40
#define numdecmyc 25
#define numcqsig 20
#define nummycsig 5
#define MAXINC_PerDec 20
#define ncalldt 150
#define MAXStatOEt 130
typedef struct
{
    double freq;
    double dt;
    bool lstate;
    QString msg;
}
xcopy_struct;
class DecoderFt8 : public QObject
{
    Q_OBJECT
public:
    explicit DecoderFt8(int id);
    ~DecoderFt8();
    void SetStMultiAnswerMod(bool f);
    void SetStWords(QString,QString,int,int,QString);
    void SetStHisCallGrid(QString,QString,QString);
    void SetStDecode(QString time,int mousebutton,bool);
    void SetStDecoderDeep(int d);
    void SetStApDecode(bool f);
    void SetBandChangedStaticAll(uint8_t);
    void SetStQSOProgress(int,int);
    void SetStTxFreq(double f);
    void Decode3intFt(bool);//2.39 remm
    void SetNewP(bool);
    void SetEndPStaticAll();//2.76.5
    void SetModeChangedStaticAll();//2.76.5
    //void SetResetPrevT(QString ptime);
    void ft8_decode(double *dd,int c_dd,double f0a,double f0b,double fqso,bool &f,int id3dec,double,double);
    int ft8_even_odd(QString);//2.76sf
    void SetVarDecodeFtPar(bool,int,int);
    void SetFreqGlobal(QString);//2.76.5

signals:
    void EmitDecodetTextFt(QStringList);
    void EmitBackColor();

private:
    static QString s_MyBaseCall8;
    static int s_id_cont_ft8_28;
    static int s_ty_cont_ft8_28;
    static QString s_cont_ft8_cq;
    static QString s_MyCall8;
    static bool s_lmycallstd;
    static bool s_lhiscallstd;
    static QString s_HisBaseCall8;
    static QString s_HisCall8;
    static QString s_HisGrid8;
    static QString s_time8;
    static int s_mousebutton8;
    static bool s_fopen8;
    static bool s_lapon8;
    static int s_nQSOProgress8;
    static int s_nlasttx;
    static double s_nftx8;
    static bool f_multi_answer_mod8;

    int decid;
    F2a f2a;
    PomAll pomAll;
    PomFt pomFt;
    GenFt8 *TGenFt8;
    double DEC_SAMPLE_RATE;
    double twopi;
    double pi;
    uint8_t band8;

    bool f_new_p;
    //QString s_time8_prev;
    int s_ndecodes;
    QString allmessages[ALL_MSG_SNR+50];
    double xdt_per_dec[ALL_MSG_SNR+20];
    double f1_save[ALL_MSG_SNR+50];
    double xdt_save[ALL_MSG_SNR+50];
    int itone_save[ALL_MSG_SNR+50][100];
    bool lsubtracted[ALL_MSG_SNR+50];
    int s_cou_dd1;
    double dd1[182600];

    bool first_sync8d;
    double complex csync_ft8_2[7][32];
    void sync8d(double complex *cd0,int i0,double complex *ctwk,int itwk,double &sync);

    bool first_ft8_downsample;
    double taper_ft8_ds[120];
    double complex cx_ft8[192100];//2.09 ->error //96000+100   //(0:NFFT1/2)  NFFT1=192000 // 96000
    void ft8_downsample(double *dd0,bool &newdat,double f1,double complex *cd0);

    double pulse_ft8_rx[5770];          //    !1920*3=5760
    double complex ctab8_[65536+10];
    void gen_ft8cwaveRx(int *i4tone,double f_tx,double complex *cwave);

    //bool first_subsft8;
    double complex cw_subsft8[190400];//180000+300  15*12000
    double endcorrectionft8[2200]; //(NFILT/2+1)  NFILT=4000; 4000/2+1=2001
    double BestIdtft8(double *dd,double f0,double dt,double idt,double complex *cref,
                      double complex *cfilt,double complex *cw_subs,double *endcorr,
                      double *xdd,double complex *cx);
    void subtractft8(double *dd,int *itone,double f0,double dt,bool refdt);

    bool first_ft8b_2;
    int mcq_ft8_2[29];
    /*int mcqfd_ft8_2[29];
    int mcqru_ft8_2[29];
    int mcqtest_ft8_2[29];
    int mcqww_ft8_2[29];
    int mcqbu_ft8_2[29];
    int mcqft_ft8_2[29];*/
    int mrrr_ft8_2[19];
    int m73_ft8_2[19];
    int mrr73_ft8_2[19];
    int cont_id0_ft8_2;
    bool one_ft8_2[9][512];//(0:511,0:8);
    QString hiscall12_0_ft8_2;
    bool ft8_downs_sync_bmet(double *,bool ap7,bool &,double &,double &,int &,int &,double s8_[79][8],
                             double *,double *,double *,double *,double *,int,int);
    void first_ft8b_and_ft8var(QString,int);
    void ft8b(double *dd,bool &newdat,int nQSOProgress,double nfqso,double nftx,int ndepth,bool n4pas3int,bool lapon,
              double napwid,bool lsubtract,bool nagain,int cid,int cty,int,int &iaptype,double &f1,double &xdt,
              double xbase,int *apsym,int &nharderrors,double &dmin,int &nbadcrc,QString &message,
              double &xsnr,QString hiscall12,int *it,int &,int &);

    void baseline(double *s,int nfa,int nfb,double *sbase);
    bool first_ft8sbl;
    double window_ft8sbl[3890];    //NFFT1=2*NSPS NSPS=1920 1920*2=3840
    void get_spectrum_baseline(double *dd,int nfa,int nfb,double *sbase);
    void sync8(double *dd,double nfa,double nfb,double syncmin,double nfqso,double s_[402][1970],double candidate[2][620],int &ncand,double *sbase);
    void ft8apset(QString mycall12,QString hiscall12,int *apsym2);//int &iaptype ,QString hisgrid6,bool bcontest,QString mygrid6,

    void PrintMsg(QString,int,double,double,QString,int,float,float,bool &,bool,bool,bool);
    QString nutc0;
    int c_zerop;
    QString msg0[2][2][MAXDEC+50];
    double dt0[2][2][MAXDEC+50];
    double f0[2][2][MAXDEC+50];
    int ndec[2][2];
    int jseq;
    void ft8_a7_save(QString,double,double,QString);
    void ft8_a7d(double *dd0,bool &newdat,QString call_1,QString call_2,QString grid4,
                 double &xdt,double &f1,double xbase,int &nharderrors,double &,QString &msg37,double &xsnr);
    double pulse_ft8_rxAp8[130];//    32*3=96
    void gen_ft8cwaveRxAp8(int *i4tone,double f_tx,double complex *cwave);
    bool s_ltry_a8;
    void ft8_a8d(double *dd,QString mycall,QString dxcall,QString dxgrid,double f1a,double &xdt,
                 double &fbest,double &xsnr,double &plog,QString &msgbest);
    void TryAp8(double *,bool,int,int,double,bool &);
//////////  FT8DecVar //////////////////////////////////////////////////
    static bool s_use_var_dec;
    //static int nintcount;
    typedef struct
    {
        double freq;
        double xdt;
    }
    tfxdec;
    tfxdec tmpcqdec[numdeccq+2];
    tfxdec tmpmyc[numdecmyc+2];

    typedef struct
    {
        double xdt;
        bool lstate;
        QString lastmsg;
    }
    lastrxmsg_;
    lastrxmsg_ lastrxmsg[1];
    typedef struct
    {
        double dt;
        QString call2;
    }
    callsigndt_struct;
    callsigndt_struct calldteven[ncalldt+4];
    callsigndt_struct calldtodd[ncalldt+4];
    typedef struct
    {
        double freq;
        double xdt;
        double complex cs_[79][8];//(0:7,79)
    }
    cq_struct;
    cq_struct evencq[numcqsig+2];
    cq_struct oddcq[numcqsig+2];
    cq_struct evenmyc[nummycsig+2];
    cq_struct oddmyc[nummycsig+2];
    cq_struct evenqso[1];
    cq_struct oddqso[1];
    cq_struct tmpcqsig[numcqsig+2];//! 20 sigs*/
    cq_struct tmpmycsig[nummycsig+2];//! 5 sigs
    cq_struct tmpqsosig[1];

    int npos;
    bool lsubtracted_;
    double freqsub[250];
    int nft8cycles;
    int ncount;
    //QString msgsrcvd[150];
    bool lrepliedother;
    bool lft8sdec;
    int ncqsignal;
    int nmycsignal;
    bool lqsomsgdcd;
    QString lasthcall;
    QString msgroot;
    int nlasttx;
    int mycalllen1;
    void ft8var_init();

    double dd8m[190000];
    double dd8[190000];

    double windowx[220];
    double windowc1[80];
    double complex csync[7][32];  //csync(0:6,32)
    double complex csynccq[8][32];//csynccq(0:7,32)
    double complex ctwkw[20][32];//(11,32)
    double complex ctwkn[20][32];//(11,32)
    double complex ctwk256[256];

    double complex csold_[79][8];//(0:7,79)
    void cwfilter_init();

    bool lhound;
    bool lskiptx1;
    bool ltxing;
    double complex csynce[19][32]; //csynce(0:18,32)
    int idtone56[56][58];//(56,58),
    int itone56[56][79];//(56,79)
    QString msg_[56];//(56)
    int idtonecqdxcns[58];//(58)
    int idtonedxcns73[58];//(58),
    int idtonefox73[58];//(58),
    int idtonespec[58];//(58)
    void tone8(bool,bool);

    bool first_apsetvar;
    QString hiscallprev;
    QString mycallprev;
    bool lhoundprev;
    int apsym_[58];
    int apcqsym_[77];//(77);
    int apsymdxns1_[58];
    int apsymdxnsrrr_[77];
    int apsymdxns732_[77];
    int apsymdxnsr73_[77];
    int apsymdxns73_[77];
    int apsymdxnsrr73_[77];
    int apsymdxstd_[58];
    int apsymmyns1_[29];
    int apsymmyns2_[58];
    int apsymmynsrrr_[77];
    int apsymmyns73_[77];
    int apsymmynsrr73_[77];
    int apsymsp_[66];
    void MakeApFlip(int *,bool *,int);
    void ft8apsetvar(bool,bool);

    int idtonemyc[58];//(58)
    void tone8myc();

    double complex cxx_ft8[193000];
    void ft8_downsamplevar(bool &,double,int,double complex *c0,double complex *c2,double complex *c3,bool,int);

    /*bool first_agccft8;
    bool lagccbail;
    double w11[1024+128];//NFFT
    void agccft8(double nfa,double nfb);*/

    void sync8var(double,double,double,double,double c[4][512],int&,int,int,int,bool,int,int,double,double);
    bool extract_calls(QString msgd,QString &c1,QString &c2,int &ispc1,int &ispc2,int &ispc3);

    double complex csyncsd[19][32];//(0:18,32)
    double complex csyncsdcq[58][32];//(0:57,32)
    int itone76[76][79];//(76,79)
    int idtone76[76][58];//(76,58)
    QString msgsd76[76];
    void tonesdvar(QString,bool);

    void twkfreq1var(double complex *ca,int npts,double fsample,double *a,double complex *cb);//,int cd_off
    void sync8dvar(double complex *cd0,int i0,double complex *ctwk,int itwk,double &sync,int ipass,
                   bool lastsync,int iqso,bool lcq,bool lcallsstd,bool lcqcand,int);

    void syncdist(double *,int *,int);
    void set_itonedem_s8(int *itonedem,bool *lmatched,double s8_1_[79][8]);
    void ft8sd1var(double s8_[79][8],int *itone,QString msgd,QString &msg37,bool &lft8sd,bool lcq);
    void ft8mf1var(double s8_[79][8],int *itone,QString msgd,QString &msg37,bool &lft8sd);
    int idtone25[25][58];//(25,58)
    void ft8mfcqvar(double s8_[79][8],int *itone,QString msgd,QString &msg37,bool &lft8sd);
    void ft8svar(double s8_[79][8],double srr,int *itone,QString &msg37,bool &lft8s,int nft8rxfsens,bool stophint);
	
    //double napwid8;
    bool lapmyc;
    //int nqsoend[3];
    void ft8sdvar(double s8_[79][8],double srr,int *itone,QString msgd,QString &msg37,bool &lft8sd,bool lcq);
    void subtractft8var(int *itone,double f0,double dt);
    
    uint8_t isFalseDecode(QString msg,int xsnr,double qual,int nharderror);   
    int nindex;
    QString msgincall[MAXINC_PerDec+5];//(174)
    double xdtincall[MAXINC_PerDec+5];//(174)
    void ft8bvar(bool &newdat1,int nQSOProgress,double nfqso,double nftx,bool lsubtract,
                 bool nagainfil,double &f1,double &xdt,int &nbadcrc,
                 QString &msg37,double &xsnr,bool stophint,bool &lFreeText,int ipass,bool lft8subpass,
                 bool &lspecial,bool lcqcand,int npass,bool lmycallstd,bool lhiscallstd,
                 bool f_od_ev,bool &lft8sd,int &i3,int nft8rxfsens,bool &lhashmsg,bool lqsothread,
                 bool lft8lowth,bool lhighsens,bool lnohiscall,bool lnomycall,bool lnohisgrid,
                 double &qual,int &iaptype2,int,double);
    void extract_callvar(QString,QString &);
    int nmsgloc;
    xcopy_struct eventmp[MAXStatOEt+5];
    xcopy_struct oddtmp[MAXStatOEt+5];
    uint8_t band8var;
    void ft8_SetStart_ev_od_var(bool,uint8_t);
    void ft8_Write_evt_odt_var(QString,int,bool,double,double,bool,bool,bool);
    void ft8_decodevar(double *,int,double,double,double,bool &,double,double,bool,bool,bool);

////////// end FT8DecVar //////////////////////////////////////////////////

};

// FT4 is one pass. MSHV does not run the FT8 three-stage timer for it.
class DecoderFt4 : public QObject
{
    Q_OBJECT
public:
    explicit DecoderFt4(int id);
    ~DecoderFt4();
    void SetStTxFreq(double f);
    void SetStMultiAnswerMod(bool f);
    void SetStDecoderDeep(int d);
    void SetStApDecode(bool f);
    void SetBandChangedStaticAll(uint8_t);
    void SetStQSOProgress(int i);
    void SetStDecode(QString time,int mousebutton,bool);
    void SetStWords(QString,QString,int,int,QString);
    void SetStHisCall(QString);
    void SetMAMCalls(QStringList ls);
    void ft4_decode(double *dd,double f0a,double f0b,double,double,double fqso,bool &f);

signals:
    void EmitDecodetTextFt(QStringList);
    void EmitBackColor();

private:
    int decid;
    F2a f2a;
    PomAll pomAll;
    PomFt pomFt;
    GenFt4 *TGenFt4;
    double DEC_SAMPLE_RATE;
    double twopi;
    double pi;
    uint8_t band4;

    bool first_ft4_ds;
    double complex cx_ft4_ds[40000];
    double window_ft4_ds[4096];
    void ft4_downsample(double *dd,bool newdata,double f0,double complex *c);

    void ft4_baseline(double *s,int nfa,int nfb,double *sbase);

    bool first_ft4detcad;
    double window_ft4[2314];
    void getcandidates4(double *dd,double fa,double fb,double,double,double syncmin,double nfqso,
                        int maxcand,double candidate[2][180],int &ncand,double*);

    bool first_ft4_sync4d;
    double complex csynca_ft4_sync[70];
    double complex csyncb_ft4_sync[70];
    double complex csyncc_ft4_sync[70];
    double complex csyncd_ft4_sync[70];
    void sync4d(double complex *cd0,int i0,double complex *ctwk,int itwk,double &sync);

    double pulse_ft4_rx[1748];
    void gen_ft4cwaveRx(int *i4tone,double f_tx,double complex *cwave);

    bool first_subsft4;
    double complex cw_subsft4[72800];
    void subtractft4(double *dd,int *itone,double f0,double dt);

    bool first_ft4bm;
    bool one_ft4_2[8][256];
    void get_ft4_bitmetrics(double complex *cd,double bitmetrics_[5][220],bool &badsync,double s4_[120][4],int,int);

    bool first_ft4d;
    int mrrr_ft4[19];
    int m73_ft4[19];
    int mrr73_ft4[19];
    int mcq_ft4[29];
    int cont_id0_ft4_2;
    QString mycall0_ft4;
    QString hiscall0_ft4;
    double fac_ft4_sync;
    double complex ctwk2_ft4_[41][70];
    int apbits_ft4[174];
    int apmy_ru_ft4[28];
    int aphis_fd_ft4[28];
    int ft4_even_odd(QString s);
    void sync_dt_df(double complex *cd2,int iseg,int &idfbest,int &ibest,double &smax,double &smax1);
    double get_xibest(double complex *cd2,int ibest,int idfbest,double smax);
    int jseq_a7;
    QString nutc0;
    int c_zerop;
    QString msg0[2][2][MAXDEC+50];
    double dt0[2][2][MAXDEC+50];
    double f0[2][2][MAXDEC+50];
    int ndec[2][2];
    void ft4_a7_save(QString,double,double,QString);
    void ft4_a7d(double *dd0,bool &newdat,QString call_1,QString call_2,QString grid4,
                 double &xdt,double &f1,double xbase,int &nharderrors,double &,QString &msg37,double &xsnr);
    void PrintMsg(QString,int nsnr,double xdt,double f1,QString message,int iaptype,float,bool &have_dec,bool &f_only_one_color,bool);
};


// FT2 is one pass at 3.4 s of its 3.75 s period.
class DecoderFt2 : public QObject
{
    Q_OBJECT
public:
    explicit DecoderFt2(int id);
    ~DecoderFt2();
    void AvgDecodeChanged(bool);
    void SetStTxFreq(double f);
    void SetStMultiAnswerMod(bool f);
    void SetStDecoderDeep(int d);
    void SetStApDecode(bool f);// only in mshv
    void SetBandChangedStaticAll(uint8_t);
    void SetStQSOProgress(int i);
    void SetStDecode(QString time,int mousebutton,bool);
    void SetStWords(QString,QString,int,int,QString);
    void SetStHisCall(QString);
    void SetMAMCalls(QStringList ls);
    //void SetNewP(bool);
    //void SetResetPrevT(QString ptime);
    void ft2_decode(double *dd,double f0a,double f0b,double,double,double fqso,bool &f);

signals:
    void EmitDecodetTextFt(QStringList);
    void EmitBackColor();

private:
    int decid;
    F2a f2a;
    PomAll pomAll;
    PomFt pomFt;
    GenFt2 *TGenFt2;
    double DEC_SAMPLE_RATE;
    double twopi;
    double pi;
    uint8_t band2;

    //bool f_new_p;
    //void dshift1(double *a,int cou_a,int ish);//???

    bool first_ft2_ds;
    double complex cx_ft2_ds[46000];     //(0:NMAX/2)=36288   31104                 [NMAX] (NMAX=21*3456)=72576
    double window_ft2_ds[6000]; //ft2=5000   (0:NFFT2-1) (NFFT2=NMAX/18)=4032     (0:NFFT2-1) (NFFT2=NMAX/16)=3888
    void ft2_downsample(double *dd,bool newdata,double f0,double complex *c);

    //void nuttal_window(double *win,int n);
    void ft2_baseline(double *s,int nfa,int nfb,double *sbase);

    bool first_ft2detcad;
    double window_ft2[2314];    //2304;//NFFT1=2048;
    void getcandidates2(double *dd,double fa,double fb,double,double,double syncmin,double nfqso,
                        int maxcand,double candidate[2][250],int &ncand,double *);

    bool first_ft2_sync4d;
    double complex csynca_ft2_sync[70];//(2*NSS) 2*32=64
    double complex csyncb_ft2_sync[70];
    double complex csyncc_ft2_sync[70];
    double complex csyncd_ft2_sync[70];
    void sync2d(double complex *cd0,int i0,double complex *ctwk,int itwk,double &sync);

    double pulse_ft2_rx[1748];          //576*3=1728    !512*3=1536
    void gen_ft2cwaveRx(int *i4tone,double f_tx,double complex *cwave);

    bool first_subsft2;
    double complex cw_subsft2[72800];//72576   =62208
    void subtractft2(double *dd,int *itone,double f0,double dt);

	void ft2_channel_est(double complex *cd,double complex *cd_eq,double *ch_snr);
    bool first_ft2bm;
    bool one_ft2_2[8][256];//(0:255,0:7)
    void get_ft2_bitmetrics(double complex *cd,double bitmetrics_[5][220],bool &badsync,double s4_[120][4],int);//2*NN=206

    bool first_ft2d;
    int mrrr_ft2[19];
    int m73_ft2[19];
    int mrr73_ft2[19];
    int mcq_ft2[29];
    int cont_id0_ft2_2;
    QString mycall0_ft2;
    QString hiscall0_ft2;
    double fac_ft2_sync;
    double complex ctwk2_ft2_[41+5][70]; //ctwk2(2*NSS,-16:16) 2*32=64
    int apbits_ft2[174];//174
    int apmy_ru_ft2[28];
    int aphis_fd_ft2[28];
    
    /*double bm_avg_[3][220];  
	int navg_ft2;
	double f_avg;
	double dt_avg;*/
	int jseq_a;
	typedef struct
    {
        int navg_ft2;
        double f_avg;
	    double dt_avg;
	    double bm_avg_[3][220];
    }
    sft2avg;
    sft2avg ft2avg[2];
    void ft2_clravg(int);	
    int ft2_even_odd(QString s);
    void sync_dt_df(double complex *cd2,int iseg,int &idfbest,int &ibest,double &smax,double &smax1);
    //int count_eq_bits(bool *a,int b_a,bool *b,int c);
    //int sync_qual(double *bitmetrics);
    double get_xibest(double complex *cd2,int ibest,int idfbest,double smax);
    //////////AP7//////////////////
    int jseq_a7;
	QString nutc0;
    int c_zerop;
	QString msg0[2][2][MAXDEC+50];
    double dt0[2][2][MAXDEC+50];
    double f0[2][2][MAXDEC+50];
    int ndec[2][2];
	void ft2_a7_save(QString,double,double,QString);
	void ft2_a7d(double *dd0,bool &newdat,QString call_1,QString call_2,QString grid4,
                 double &xdt,double &f1,double xbase,int &nharderrors,double &,QString &msg37,double &xsnr);	
	//////////end AP7//////////////////	
    void PrintMsg(QString,int nsnr,double xdt,double f1,QString message,int iaptype,bool,float,bool &have_dec,bool &f_only_one_color,bool);
    uint8_t isFalseDecode(QString,int,double,bool,int,int,int);
	void make_bm_ap(double *,double bitmetrics_[5][220],int,bool,int,int,double,double,double,bool,
							bool,double,int &,QString *,int,int &,bool &,bool &,bool);					
};

// moc's generated file includes this header and then Qt's moc helpers.
// complex.h's I macro collides with a template parameter named I.
#ifdef STD_FT8_MOC
#undef I
#undef complex
#endif
#endif


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                       */
/*    This file is part of the HiGHS linear optimization suite           */
/*                                                                       */
/*    Written and engineered 2008-2022 at the University of Edinburgh    */
/*                                                                       */
/*    Available as open-source under the MIT License                     */
/*                                                                       */
/*    Authors: Julian Hall, Ivet Galabova, Leona Gottwald and Michael    */
/*    Feldmeier                                                          */
/*                                                                       */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* HiGHS link code */

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* GAMS API */
#include "gevmcc.h"
#include "gmomcc.h"

typedef struct optRec* optHandle_t;

/* HiGHS API */
#include "Highs.h"
#include "HighsIO.h"
#include "io/FilereaderLp.h"
#include "io/FilereaderMps.h"
#include "io/LoadOptions.h" /* for loadOptionsFromFile */

#if defined(_WIN32)
#if !defined(STDCALL)
#define STDCALL __stdcall
#endif
#if !defined(DllExport)
#define DllExport __declspec(dllexport)
#endif
#else
#if !defined(STDCALL)
#define STDCALL
#endif
#if !defined(DllExport)
#define DllExport
#endif
#endif

struct gamshighs_s {
  gmoHandle_t gmo;
  gevHandle_t gev;
  HighsInt debug;

  Highs* highs;
  HighsLp* lp;
  HighsOptions* options;
};
typedef struct gamshighs_s gamshighs_t;

static void gevprint(HighsInt level, const char* msg, void* msgcb_data) {
  gevHandle_t gev = (gevHandle_t)msgcb_data;
  gevLogPChar(gev, msg);
}

static void gevlog(HighsLogType type, const char* msg, void* msgcb_data) {
  gevHandle_t gev = (gevHandle_t)msgcb_data;
  if (type == HighsLogType::kInfo)
    gevLogPChar(gev, msg);
  else
    gevLogStatPChar(gev, msg);
}

static enum gmoVarEquBasisStatus translateBasisStatus(HighsBasisStatus status) {
  switch (status) {
    case HighsBasisStatus::kBasic:
      return gmoBstat_Basic;
    case HighsBasisStatus::kLower:
      return gmoBstat_Lower;
    case HighsBasisStatus::kNonbasic:
    case HighsBasisStatus::SUPER:
    case HighsBasisStatus::kZero:
      return gmoBstat_Super;
    case HighsBasisStatus::kUpper:
      return gmoBstat_Upper;
  }
  // this should never happen
  return gmoBstat_Super;
}

static HighsBasisStatus translateBasisStatus(enum gmoVarEquBasisStatus status) {
  switch (status) {
    case gmoBstat_Basic:
      return HighsBasisStatus::kBasic;
    case gmoBstat_Lower:
      return HighsBasisStatus::kLower;
    case gmoBstat_Super:
      return HighsBasisStatus::SUPER;
    case gmoBstat_Upper:
      return HighsBasisStatus::kUpper;
  }
  // this should never happen
  return HighsBasisStatus::SUPER;
}

static HighsInt setupOptions(gamshighs_t* gh) {
  assert(gh != NULL);
  assert(gh->options == NULL);

  gh->options = new HighsOptions;

  gh->options->time_limit = gevGetDblOpt(gh->gev, gevResLim);
  if (gevGetIntOpt(gh->gev, gevIterLim) != ITERLIM_INFINITY)
    gh->options->simplex_iteration_limit = gevGetIntOpt(gh->gev, gevIterLim);

  if (gevGetIntOpt(gh->gev, gevUseCutOff))
    gh->options->objective_bound = gevGetDblOpt(gh->gev, gevCutOff);

  if (gmoOptFile(gh->gmo) > 0) {
    char optfilename[GMS_SSSIZE];
    gmoNameOptFile(gh->gmo, optfilename);
    if (!loadOptionsFromFile(*gh->options, optfilename)) return 1;
  }

  gh->options->printmsgcb = gevprint;
  gh->options->logmsgcb = gevlog;
  gh->options->msgcb_data = (void*)gh->gev;
  highsSetLogCallback(*gh->options);

  return 0;
}

static HighsInt setupProblem(gamshighs_t* gh) {
  HighsInt numCol;
  HighsInt numRow;
  HighsInt numNz;
  HighsInt i;
  HighsInt rc = 1;
  HighsSolution sol;

  assert(gh != NULL);
  assert(gh->options != NULL);
  assert(gh->highs == NULL);
  assert(gh->lp == NULL);

  gh->highs = new Highs(*gh->options);

  numCol = gmoN(gh->gmo);
  numRow = gmoM(gh->gmo);
  numNz = gmoNZ(gh->gmo);

  gh->lp = new HighsLp();

  gh->lp->num_row_ = numRow;
  gh->lp->num_col_ = numCol;
  //  gh->lp->nnz_ = numNz;

  /* columns */
  gh->lp->col_upper_.resize(numCol);
  gh->lp->col_lower_.resize(numCol);
  gmoGetVarLower(gh->gmo, &gh->lp->col_lower_[0]);
  gmoGetVarUpper(gh->gmo, &gh->lp->col_upper_[0]);

  /* objective */
  gh->lp->col_cost_.resize(numCol);
  gmoGetObjVector(gh->gmo, &gh->lp->col_cost_[0], NULL);
  if (gmoSense(gh->gmo) == gmoObj_Min)
    gh->lp->sense_ = ObjSense::kMinimize;
  else
    gh->lp->sense_ = ObjSense::kMaximize;
  gh->lp->offset_ = gmoObjConst(gh->gmo);

  /* row left- and right-hand-side */
  gh->lp->row_lower_.resize(numRow);
  gh->lp->row_upper_.resize(numRow);
  for (i = 0; i < numRow; ++i) {
    switch (gmoGetEquTypeOne(gh->gmo, i)) {
      case gmoequ_E:
        gh->lp->row_lower_[i] = gh->lp->row_upper_[i] =
            gmoGetRhsOne(gh->gmo, i);
        break;

      case gmoequ_G:
        gh->lp->row_lower_[i] = gmoGetRhsOne(gh->gmo, i);
        gh->lp->row_upper_[i] = kHighsInf;
        break;

      case gmoequ_L:
        gh->lp->row_lower_[i] = -kHighsInf;
        gh->lp->row_upper_[i] = gmoGetRhsOne(gh->gmo, i);
        break;

      case gmoequ_N:
      case gmoequ_X:
      case gmoequ_C:
      case gmoequ_B:
        /* these should not occur */
        goto TERMINATE;
    }
  }

  /* coefficients matrix */
  gh->lp->a_matrix_.start_.resize(numCol + 1);
  gh->lp->a_matrix_.index_.resize(numNz);
  gh->lp->a_matrix_.value_.resize(numNz);
  gmoGetMatrixCol(gh->gmo, &gh->lp->a_matrix_.start_[0],
                  &gh->lp->a_matrix_.index_[0], &gh->lp->a_matrix_.value_[0],
                  NULL);

  gh->highs->passModel(*gh->lp);

  // FilereaderLp().writeModelToFile("highs.lp", *gh->lp);
  // FilereaderMps().writeModelToFile("highs.mps", *gh->lp);

  // pass initial solution
  sol.col_value.resize(numCol);
  sol.col_dual.resize(numCol);
  sol.row_value.resize(numRow);
  sol.row_dual.resize(numRow);
  gmoGetVarL(gh->gmo, &sol.col_value[0]);
  gmoGetVarM(gh->gmo, &sol.col_dual[0]);
  gmoGetEquL(gh->gmo, &sol.row_value[0]);
  gmoGetEquM(gh->gmo, &sol.row_dual[0]);
  gh->highs->setSolution(sol);

  if (gmoHaveBasis(gh->gmo)) {
    HighsBasis basis;
    basis.col_status.resize(numCol);
    basis.row_status.resize(numRow);
    HighsInt nbasic = 0;

    for (HighsInt i = 0; i < numCol; ++i) {
      basis.col_status[i] = translateBasisStatus(
          (enum gmoVarEquBasisStatus)gmoGetVarStatOne(gh->gmo, i));
      if (basis.col_status[i] == HighsBasisStatus::kBasic) ++nbasic;
    }

    for (HighsInt i = 0; i < numRow; ++i) {
      basis.row_status[i] = translateBasisStatus(
          (enum gmoVarEquBasisStatus)gmoGetEquStatOne(gh->gmo, i));
      if (basis.row_status[i] == HighsBasisStatus::kBasic) ++nbasic;
    }

    basis.valid = nbasic == numRow;
    /* HiGHS compiled without NDEBUG defined currently raises an assert in
     * basisOK() if given an invalid basis */
    if (basis.valid) gh->highs->setBasis(basis);
  }

  rc = 0;
TERMINATE:

  return rc;
}

static HighsInt processSolve(gamshighs_t* gh) {
  assert(gh != NULL);
  assert(gh->highs != NULL);
  assert(gh->lp != NULL);

  gmoHandle_t gmo = gh->gmo;
  Highs* highs = gh->highs;

  gmoSetHeadnTail(gmo, gmoHresused, gevTimeDiffStart(gh->gev));
  gmoSetHeadnTail(gmo, gmoHiterused, highs->getInfo().simplex_iteration_count);

  // figure out model and solution status and whether we should have a solution
  // to be written
  bool writesol = false;
  switch (highs->getModelStatus()) {
    case HighsModelStatus::kNotset:
    case HighsModelStatus::kLoadError:
    case HighsModelStatus::kModelError:
    case HighsModelStatus::kPresolveError:
    case HighsModelStatus::kSolveError:
    case HighsModelStatus::kPostsolveError:
      gmoModelStatSet(gmo, gmoModelStat_ErrorNoSolution);
      gmoSolveStatSet(gmo, gmoSolveStat_SolverErr);
      break;

    case HighsModelStatus::kModelEmpty:
      gmoModelStatSet(gmo, gmoModelStat_NoSolutionReturned);
      gmoSolveStatSet(gmo, gmoSolveStat_Solver);
      break;

    case HighsModelStatus::kOptimal:
      gmoModelStatSet(gmo, gmoModelStat_OptimalGlobal);
      gmoSolveStatSet(gmo, gmoSolveStat_Normal);
      writesol = true;
      break;

    case HighsModelStatus::kInfeasible:
      // TODO is there an infeasible solution to write?
      // gmoModelStatSet(gmo, havesol ? gmoModelStat_InfeasibleGlobal :
      // gmoModelStat_InfeasibleNoSolution);
      gmoModelStatSet(gmo, gmoModelStat_InfeasibleNoSolution);
      gmoSolveStatSet(gmo, gmoSolveStat_Normal);
      break;

    case HighsModelStatus::kUnboundedOrInfeasible:
      // TODO is there a (feasible) solution to write?
      // gmoModelStatSet(gmo, havesol ? gmoModelStat_Unbounded :
      // gmoModelStat_UnboundedNoSolution);
      gmoModelStatSet(gmo, gmoModelStat_NoSolutionReturned);
      gmoSolveStatSet(gmo, gmoSolveStat_Normal);
      break;

    case HighsModelStatus::kUnbounded:
      // TODO is there a (feasible) solution to write?
      // gmoModelStatSet(gmo, havesol ? gmoModelStat_Unbounded :
      // gmoModelStat_UnboundedNoSolution);
      gmoModelStatSet(gmo, gmoModelStat_UnboundedNoSolution);
      gmoSolveStatSet(gmo, gmoSolveStat_Normal);
      break;

    case HighsModelStatus::kObjectiveBound:
      // TODO is there a solution to write and is it feasible?
      // gmoModelStatSet(gmo, havesol ? gmoModelStat_InfeasibleIntermed :
      // gmoModelStat_NoSolutionReturned);
      gmoModelStatSet(gmo, gmoModelStat_NoSolutionReturned);
      gmoSolveStatSet(gmo, gmoSolveStat_Solver);
      break;

    case HighsModelStatus::kObjectiveTarget:
      // TODO is there a solution to write and is it feasible?
      // gmoModelStatSet(gmo, havesol ? gmoModelStat_InfeasibleIntermed :
      // gmoModelStat_NoSolutionReturned);
      gmoModelStatSet(gmo, gmoModelStat_NoSolutionReturned);
      gmoSolveStatSet(gmo, gmoSolveStat_Solver);
      break;

    case HighsModelStatus::kTimeLimit:
      // TODO is there an (feasible) solution to write?
      // gmoModelStatSet(gmo, havesol ? gmoModelStat_InfeasibleIntermed :
      // gmoModelStat_NoSolutionReturned);
      gmoModelStatSet(gmo, gmoModelStat_NoSolutionReturned);
      gmoSolveStatSet(gmo, gmoSolveStat_Resource);
      break;

    case HighsModelStatus::kIterationLimit:
      // TODO is there an (feasible) solution to write?
      // gmoModelStatSet(gmo, havesol ? gmoModelStat_InfeasibleIntermed :
      // gmoModelStat_NoSolutionReturned);
      gmoModelStatSet(gmo, gmoModelStat_NoSolutionReturned);
      gmoSolveStatSet(gmo, gmoSolveStat_Iteration);
      break;

    case HighsModelStatus::kUnknown:
      // TODO is there an (feasible) solution to write?
      // gmoModelStatSet(gmo, havesol ? gmoModelStat_InfeasibleIntermed :
      // gmoModelStat_NoSolutionReturned);
      gmoModelStatSet(gmo, gmoModelStat_NoSolutionReturned);
      gmoSolveStatSet(gmo, gmoSolveStat_Iteration);
      break;
  }

  if (writesol) {
    const HighsSolution& sol = highs->getSolution();
    assert((HighsInt)sol.col_value.size() == gmoN(gmo));
    assert((HighsInt)sol.col_dual.size() == gmoN(gmo));
    assert((HighsInt)sol.row_value.size() == gmoM(gmo));
    assert((HighsInt)sol.row_dual.size() == gmoM(gmo));

    const HighsBasis& basis = highs->getBasis();
    assert(!basis.valid || (HighsInt)basis.col_status.size() == gmoN(gmo));
    assert(!basis.valid || (HighsInt)basis.row_status.size() == gmoM(gmo));

    for (HighsInt i = 0; i < gmoN(gmo); ++i) {
      gmoVarEquBasisStatus basisstat;
      if (basis.valid)
        basisstat = translateBasisStatus(basis.col_status[i]);
      else
        basisstat = gmoBstat_Super;

      // TODO change when we can process infeasible or unbounded solutions
      gmoVarEquStatus stat = gmoCstat_OK;

      gmoSetSolutionVarRec(gmo, i, sol.col_value[i], sol.col_dual[i], basisstat,
                           stat);
    }

    for (HighsInt i = 0; i < gmoM(gmo); ++i) {
      gmoVarEquBasisStatus basisstat;
      if (basis.valid)
        basisstat = translateBasisStatus(basis.row_status[i]);
      else
        basisstat = gmoBstat_Super;

      // TODO change when we can process infeasible or unbounded solutions
      gmoVarEquStatus stat = gmoCstat_OK;

      gmoSetSolutionEquRec(gmo, i, sol.row_value[i], sol.row_dual[i], basisstat,
                           stat);
    }

    // if there were =N= rows (lp08), then gmoCompleteObjective wouldn't get
    // their activity right
    // gmoCompleteObjective(gmo,
    // highs->getInfo().objective_function_value);
    gmoCompleteSolution(gmo);
  }

  return 0;
}

extern "C" {

void his_Initialize(void) {
  gmoInitMutexes();
  gevInitMutexes();
}

void his_Finalize(void) {
  gmoFiniMutexes();
  gevFiniMutexes();
}

DllExport void STDCALL hisXCreate(void** Cptr) {
  assert(Cptr != NULL);

  *Cptr = calloc(1, sizeof(gamshighs_t));
}

DllExport HighsInt STDCALL hiscreate(void** Cptr, char* msgBuf,
                                     HighsInt msgBufLen) {
  assert(Cptr != NULL);
  assert(msgBufLen > 0);
  assert(msgBuf != NULL);

  *Cptr = calloc(1, sizeof(gamshighs_t));

  msgBuf[0] = 0;

  return 1;
}

DllExport void STDCALL hisXFree(void** Cptr) {
  assert(Cptr != NULL);
  assert(*Cptr != NULL);

  free(*Cptr);
  *Cptr = NULL;

  gmoLibraryUnload();
  gevLibraryUnload();
}

DllExport HighsInt STDCALL hisfree(void** Cptr) {
  hisXFree(Cptr);

  return 1;
}

/* comp returns the compatibility mode:
           0: client is too old for the DLL, no compatibility
           1: client version and DLL version are the same, full compatibility
           2: client is older than DLL, but defined as compatible, backward
   compatibility 3: client is newer than DLL, forward compatibility
           FIXME: for now, we just claim full compatibility
 */
DllExport HighsInt STDCALL C__hisXAPIVersion(HighsInt api, char* Msg,
                                             HighsInt* comp) {
  *comp = 1;
  return 1;
}

DllExport HighsInt STDCALL D__hisXAPIVersion(HighsInt api, char* Msg,
                                             HighsInt* comp) {
  *comp = 1;
  return 1;
}

DllExport HighsInt STDCALL C__hisXCheck(const char* funcn, HighsInt ClNrArg,
                                        HighsInt Clsign[], char* Msg) {
  return 1;
}

DllExport HighsInt STDCALL D__hisXCheck(const char* funcn, HighsInt ClNrArg,
                                        HighsInt Clsign[], char* Msg) {
  return 1;
}

DllExport HighsInt STDCALL C__hisReadyAPI(void* Cptr, gmoHandle_t Gptr,
                                          optHandle_t Optr) {
  gamshighs_t* gh;

  assert(Cptr != NULL);
  assert(Gptr != NULL);
  assert(Optr == NULL);

  char msg[256];
  if (!gmoGetReady(msg, sizeof(msg))) return 1;
  if (!gevGetReady(msg, sizeof(msg))) return 1;

  gh = (gamshighs_t*)Cptr;
  gh->gmo = Gptr;
  gh->gev = (gevHandle_t)gmoEnvironment(gh->gmo);

  return 0;
}

#define XQUOTE(x) QUOTE(x)
#define QUOTE(x) #x

DllExport HighsInt STDCALL C__hisCallSolver(void* Cptr) {
  HighsInt rc = 1;
  gamshighs_t* gh;
  HighsStatus status;

  gh = (gamshighs_t*)Cptr;
  assert(gh->gmo != NULL);
  assert(gh->gev != NULL);

  gevLogStatPChar(gh->gev, "HiGHS " XQUOTE(HIGHS_VERSION_MAJOR) "." XQUOTE(HIGHS_VERSION_MINOR) "." XQUOTE(HIGHS_VERSION_PATCH) " [date: " HIGHS_COMPILATION_DATE ", git hash: " HIGHS_GITHASH "]\n");
  gevLogStatPChar(gh->gev,
                  "Copyright (c) 2020 ERGO-Code under MIT license terms.\n");

  gmoModelStatSet(gh->gmo, gmoModelStat_NoSolutionReturned);
  gmoSolveStatSet(gh->gmo, gmoSolveStat_SystemErr);

  /* get the problem into a normal form */
  gmoObjStyleSet(gh->gmo, gmoObjType_Fun);
  gmoObjReformSet(gh->gmo, 1);
  gmoIndexBaseSet(gh->gmo, 0);
  gmoSetNRowPerm(gh->gmo); /* hide =N= rows */
  gmoMinfSet(gh->gmo, -kHighsInf);
  gmoPinfSet(gh->gmo, kHighsInf);

  if (setupOptions(gh)) goto TERMINATE;

  if (setupProblem(gh)) goto TERMINATE;

  gevTimeSetStart(gh->gev);

  /* solve the problem */
  status = gh->highs->run();
  if (status != HighsStatus::kOk) goto TERMINATE;

  /* pass solution, status, etc back to GMO */
  if (processSolve(gh)) goto TERMINATE;

  rc = 0;
TERMINATE:

  delete gh->lp;
  gh->lp = NULL;

  delete gh->highs;
  gh->highs = NULL;

  delete gh->options;
  gh->options = NULL;

  return rc;
}

DllExport HighsInt STDCALL C__hisHaveModifyProblem(void* Cptr) { return 1; }

DllExport HighsInt STDCALL C__hisModifyProblem(void* Cptr) {
  gamshighs_t* gh = (gamshighs_t*)Cptr;
  assert(gh != NULL);

  /* set the GMO styles, in case someone changed this */
  gmoObjStyleSet(gh->gmo, gmoObjType_Fun);
  gmoObjReformSet(gh->gmo, 1);
  gmoIndexBaseSet(gh->gmo, 0);
  gmoSetNRowPerm(gh->gmo); /* hide =N= rows */
  gmoMinfSet(gh->gmo, -kHighsInf);
  gmoPinfSet(gh->gmo, kHighsInf);

  HighsInt maxsize = std::max(gmoN(gh->gmo), gmoM(gh->gmo));

  HighsInt jacnz;
  gmoGetJacUpdate(gh->gmo, NULL, NULL, NULL, &jacnz);
  if (jacnz + 1 > maxsize) maxsize = jacnz + 1;

  HighsInt* colidx = new int[maxsize];
  HighsInt* rowidx = new int[maxsize];
  double* array1 = new double[maxsize];
  double* array2 = new double[maxsize];

  Highs* highs = gh->highs;
  assert(highs != NULL);

  // update objective coefficients
  HighsInt nz;
  HighsInt nlnz;
  gmoGetObjSparse(gh->gmo, colidx, array1, NULL, &nz, &nlnz);
  assert(nlnz == gmoObjNZ(gh->gmo));
  highs->changeColsCost(nz, colidx, array1);

  // TODO update objective offset

  // update variable bounds
  gmoGetVarLower(gh->gmo, array1);
  gmoGetVarUpper(gh->gmo, array2);
  highs->changeColsBounds(0, gmoN(gh->gmo), array1, array2);

  // update constraint sides
  for (HighsInt i = 0; i < gmoM(gh->gmo); ++i) {
    double rhs = gmoGetRhsOne(gh->gmo, i);
    rowidx[i] = 1;
    switch (gmoGetEquTypeOne(gh->gmo, i)) {
      case gmoequ_E:
        array1[i] = rhs;
        array2[i] = rhs;
        break;

      case gmoequ_G:
        array1[i] = rhs;
        array2[i] = kHighsInf;
        break;

      case gmoequ_L:
        array1[i] = -kHighsInf;
        array2[i] = rhs;
        break;

      case gmoequ_N:
      case gmoequ_X:
      case gmoequ_C:
      case gmoequ_B:
        /* these should not occur */
        rowidx[i] = 0;
        break;
    }
  }
  highs->changeRowsBounds(rowidx, array1, array2);

  // update constraint matrix
  gmoGetJacUpdate(gh->gmo, rowidx, colidx, array1, &jacnz);
  for (HighsInt i = 0; i < nz; ++i)
    highs->changeCoeff(rowidx[i], colidx[i], array1[i]);

  delete[] array2;
  delete[] array1;
  delete[] rowidx;
  delete[] colidx;

  return 0;
}

void his_Initialize(void);
void his_Finalize(void);

}  // extern "C"

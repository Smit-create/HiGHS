#include "catch.hpp"
#include "Highs.h"
#include "HighsLp.h"
#include "HighsOptions.h"
#include "HighsIO.h"
#include "HighsStatus.h"
#include "HighsTimer.h"
#include "HighsLpUtils.h"
#include "HighsSimplexInterface.h"
#include "Avgas.h"

// No commas in test case name.
TEST_CASE("LP-validation", "[highs_data]") {

  // Create an empty LP
  HighsLp lp;
  HighsOptions options;
  HighsTimer timer;
  HighsStatus return_status;
  HighsSetMessagelevel(ML_ALWAYS);

  Avgas avgas;
  int num_row;
  vector<double> rowLower;
  vector<double> rowUpper;
  avgas.rows(num_row, rowLower, rowUpper);

  int num_col = 0;
  int num_nz = 0;
  vector<double> colCost;
  vector<double> colLower;
  vector<double> colUpper;
  vector<int> Astart;
  vector<int> Aindex;
  vector<double> Avalue;
  for (int col = 0; col < 8; col++) {
    avgas.col(col, num_col, num_nz, colCost, colLower, colUpper, Astart, Aindex, Avalue);
  }

  return_status = checkLp(lp);
  REQUIRE(return_status == HighsStatus::OK);
  bool normalise = true;
  return_status = assessLp(lp, options, normalise);
  REQUIRE(return_status == HighsStatus::OK);
  reportLp(lp);

  const double my_infinity = 1e30;
  HighsModelObject hmo(lp, options, timer);
  HighsSimplexInterface hsi(hmo);

  return_status = hsi.util_add_rows(num_row, &rowLower[0], &rowUpper[0], 0, NULL, NULL, NULL);
  //  printf("util_add_rows: return_status = %s\n", HighsStatusToString(return_status).c_str());
  REQUIRE(return_status == HighsStatus::Info);
  reportLp(lp);
  
  return_status = hsi.util_add_cols(num_col, &colCost[0], &colLower[0], &colUpper[0],
				    num_nz, &Astart[0], &Aindex[0], &Avalue[0]);
  printf("util_add_cols: return_status = %s\n", HighsStatusToString(return_status).c_str());
  REQUIRE(return_status == HighsStatus::OK);
  reportLp(lp);
  

  /*
  // Create an empty column
  int XnumNewCol = 1;
  int XnumNewNZ = 0;
  vector<double> XcolCost; XcolCost.resize(XnumNewCol); XcolCost[0] = 1;
  vector<double> XcolLower; XcolLower.resize(XnumNewCol); XcolLower[0] = 0;
  vector<double> XcolUpper; XcolUpper.resize(XnumNewCol); XcolUpper[0] = 1e25;
  vector<int> XAstart; XAstart.resize(XnumNewCol);
  vector<int> XAindex; 
  vector<double> XAvalue;
  // Add an empty column
  return_status = hsi.util_add_cols(XnumNewCol, &XcolCost[0], &XcolLower[0], &XcolUpper[0],
				    XnumNewNZ, &XAstart[0], &XAindex[0], &XAvalue[0]);
  REQUIRE(return_status == HighsStatus::Info);
  XcolUpper[0] = my_infinity;
  reportLp(lp);

  // Try to add a column with illegal cost
  XcolCost[0] = my_infinity;
  return_status = hsi.util_add_cols(XnumNewCol, &XcolCost[0], &XcolLower[0], &XcolUpper[0],
				    XnumNewNZ, &XAstart[0], &XAindex[0], &XAvalue[0]);
  REQUIRE(return_status == HighsStatus::Error);
  XcolCost[0] = -my_infinity;
  return_status = hsi.util_add_cols(XnumNewCol, &XcolCost[0], &XcolLower[0], &XcolUpper[0],
				    XnumNewNZ, &XAstart[0], &XAindex[0], &XAvalue[0]);
  REQUIRE(return_status == HighsStatus::Error);
  XcolCost[0] = 1;

  // Add a column with bound inconsistency due to upper
  XcolUpper[0] = -my_infinity;
  return_status = hsi.util_add_cols(XnumNewCol, &XcolCost[0], &XcolLower[0], &XcolUpper[0],
				    XnumNewNZ, &XAstart[0], &XAindex[0], &XAvalue[0]);
  REQUIRE(return_status == HighsStatus::Warning);
  XcolUpper[0] = 0;

  reportLp(lp);

  // Add a column with bound inconsistency due to lower
  XcolLower[0] = my_infinity;
  return_status = hsi.util_add_cols(XnumNewCol, &XcolCost[0], &XcolLower[0], &XcolUpper[0],
				    XnumNewNZ, &XAstart[0], &XAindex[0], &XAvalue[0]);
  REQUIRE(return_status == HighsStatus::Warning);
  XcolLower[0] = 0;

  reportLp(lp);

  // Add a legitimate column
  XcolLower[0] = 0;
  return_status = hsi.util_add_cols(XnumNewCol, &XcolCost[0], &XcolLower[0], &XcolUpper[0],
				    XnumNewNZ, &XAstart[0], &XAindex[0], &XAvalue[0]);
  REQUIRE(return_status == HighsStatus::OK);

  reportLp(lp);
  /*
  Highs highs(options);
  
  HighsStatus init_status = highs.initializeLp(lp);
  REQUIRE(return_status == HighsStatus::OK);

  HighsStatus run_status = highs.run();
  REQUIRE(run_status == HighsStatus::OK);
  */
}


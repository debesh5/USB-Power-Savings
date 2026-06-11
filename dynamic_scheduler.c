#include <stdio.h>
#include <math.h>
#include "hub_config.h"

#include <time.h>
#include <sys/time.h>
#include <sys/utsname.h>

main()
{
  struct timeval start_time, end_time;

  int up_width, up_freq; //UP width and freq (note 5 GT/s changed to 4 GT/s)
  int num_dp; // number of downstream ports
  int dp_usb[15];// Type of USB:1, 2, 3, 4
  int dp_width[15], dp_freq[15]; // Only meaningful for USB3 and above; else 0
  double dp_up_bw_demand[15], dp_dn_bw_demand[15]; // Projected B/W demand in each direction
  double net_up_demand=0, net_dn_demand=0; // represents projected b/w demand per direction from all the DPs (util * raw b/w)
  double up_bw;  //total b/w available per direction in UP
  double dp_raw_bw_aggr; // Raw B/W total of all DPs in either direction
  double raw_bw_allot[15]; // Raw B/W allottment if each DP requested full B/W
  double dp_actual_bw_allot_up[15],dp_actual_bw_allot_dn[15]; // Actual b/w allocated to each DP by the host/hub depending on their demand and their fair share of b/w allocation

  // The following are used during the b/w allocation

  double aggr_bw_allotted_up=0,aggr_bw_allotted_dn=0;
  // Used during b/w allocation to count how much has been allotted this far
  short int dp_bw_demand_met_up[15],dp_bw_demand_met_dn[15];
  // The above is used to denote whether b/w demand is fully met or not
  // The following two variables are the remainder
  // Following two variable: For each DP/direction, we count if the
  // b/w is allocated or not after first pass - if yes, that DP's ratio is
  // made 0 - these represent the remaining ratios added - so in the second
  // pass we do the proportionate b/w distribution to those ports whose
  // full demand can not be met

  double raw_bw_allot_remain_after_first_pass_up=0; 
  double raw_bw_allot_remain_after_first_pass_dn=0;
  // These are the actual b/w remaining after first pass

  double bw_remain_up_after_first_pass;
  double bw_remain_dn_after_first_pass;
  // Actual utilization after the 2-pass b/w allocation

  double dp_util_up[15],dp_util_dn[15],dp_util_link_U2U3[15],dp_util_link_U1[15];
  double up_util_up, up_util_dn, up_util_link_U2U3,up_util_link_U1;
  double time_u0_or_entry_exit_u1,time_u0_or_entry_exit_u2,time_u0_or_entry_exit_u3,time_tot;
  double total_power_savings_hub_u1, frac_u1; // Power savings and fractional time with U1
  double total_power_savings_hub_u2, frac_u2; // Power savings and fractional time with U2
  double total_power_savings_hub_u3, frac_u3; //// Power savings and fractional time with U3
  double power_savings_port_u1, power_savings_port_u2, power_savings_port_u3; // partial per-port calculation towards the above
 
  int i,j; //iteratives for algorithm
  double raw_bw_allot_tot=0;
  
  // Input Data Formatting
  FILE *fp1;
  
  printf("-----------------------------------\n");
  printf("Enter the Hub configuration in `hub_config.in' file\n");
  printf("Expected format: Upstream Port (UP) width, frequency for USB3+ (expected to have USB2) \n \t followed by no of downstream ports followed by for DP the USB Type, width, frequency, \n\t Upstream B/W demand, Downstream B/W demand (example below)\n hub_config.in Format as follows ...\n");
 
  printf("2, 20 // Line 1: Means x2 at 20G for UP\n");
 
  printf("4 // Line 2: No of downstream Ports in Hub is 4 - this means 4 more lines as follows\n");

  printf("3 1 5 2.0 0.4: Downstream Port 0 is USB 3; 1 wide; 5 GT/s, \n\tUpstream B/W demand 2.0 Gb/s, and Downstream B/W demand of 0.4 Gb/s\n");

  printf("2 0 0 0.5 0.1: Downstream Port 1 is USB 2; next two numbers are dont care, \n\t Upstream utilization of 0.5, and DOwnstream Utilization of 0.1\n");

  printf("3 2 10 10.0 18.0: Downstream Port 2 is USB 3.2; 2 wide; 10 GT/s, \n\t Upstream B/W demand is 10 Gb/s, and Downstream B/W demand is 18.0 Gb/s\n");

  printf("4 2 20 20.0 36.0: Downstream Port 3 is USB 4; 2 wide; 20 GT/s, \n\t Upstream B/W demand is 20.0 Gb/s, and Downstream B/W demand is 36.0 Gb/s\n");

  printf("-----------------------------------\n");

  // Read from input file and print out

  fp1=fopen("hub_config.in","r");

  fscanf(fp1, "%d %d", &up_width,&up_freq);

  if(up_freq == 5) up_freq =4;

  up_bw=(double)up_freq * (double)up_width;

  fscanf(fp1, "%d", &num_dp);

  printf("USB Hub Specification considered:\n");

  printf("UP: x%d, %dGT/s, %d Downstream Ports\n",up_width,up_freq,num_dp);

  dp_raw_bw_aggr = 0;

  for(i=0;i<num_dp;i++){

    fscanf(fp1,"%d %d %d %lf %lf",&dp_usb[i],&dp_width[i],&dp_freq[i],&dp_up_bw_demand[i],&dp_dn_bw_demand[i]);

    if(dp_usb[i]<3){

      dp_width[i]=0;

      dp_freq[i]=0;

    }

    if(dp_freq[i]==5) dp_freq[i]=4;

    dp_raw_bw_aggr += (double)(dp_width[i]*dp_freq[i]);

    printf("DP %d: USB %d, width: %d, %d GT/s, Util (%lf up, %lf dn)\n",i,dp_usb[i],dp_width[i],dp_freq[i],dp_up_bw_demand[i],dp_dn_bw_demand[i]);

    net_up_demand+= dp_up_bw_demand[i];

    net_dn_demand+= dp_dn_bw_demand[i];

    //printf("i=%d, up = %lf, dn = %lf\n",i,net_up_demand,net_dn_demand);

  }

  fclose(fp1);

  printf("Net UP B/W (%lf up, %lf dn) vs net demand by DP ports (%lf up, %lf dn) vs raw b/w demand in DP %lf in each direction\n",up_bw,up_bw,net_up_demand,net_dn_demand,dp_raw_bw_aggr);

   

  printf("----Raw B/W Allocation to DP based on width/ frequency -------\n");
  gettimeofday(&start_time, NULL);
	 
  for(i=0;i<num_dp;i++){

    raw_bw_allot[i]=(double)(dp_width[i]*dp_freq[i])/dp_raw_bw_aggr;

    printf("DP Port %d: Raw Util: %lf\n",i,raw_bw_allot[i]);

    raw_bw_allot_tot+=raw_bw_allot[i];

  }

  printf("Total Raw B/W allot fraction: %lf\n",raw_bw_allot_tot);

  printf("---------------------\n");

  //

  // Now the actual b/w allocation to each DP in each direction

  // based on the actual b/w demand and the fair b/w allocation

  // (lower of the two). This happens in two phases. In the first

  // phase we do the lower of the two and then any remaining b/w

  // from the available UP b/w is redistributed proportionately among

  // those DP ports whose b/w demand is more than the fair allocation

  //

  //

  printf("-----First Pass B/W allocation to DP ports-----\n");

  for(i=0;i<num_dp;i++){

    if((raw_bw_allot[i]*up_bw) < dp_up_bw_demand[i]){

      dp_actual_bw_allot_up[i]=0;

      dp_bw_demand_met_up[i]=0;

      raw_bw_allot_remain_after_first_pass_up += raw_bw_allot[i];

    }

    else{

      dp_actual_bw_allot_up[i]=dp_up_bw_demand[i];

      dp_bw_demand_met_up[i]=1;

    }


    if((raw_bw_allot[i]*up_bw) < dp_dn_bw_demand[i]){

      dp_actual_bw_allot_dn[i]=0;

      dp_bw_demand_met_dn[i]=0;

      raw_bw_allot_remain_after_first_pass_dn += raw_bw_allot[i];

    }

    else{

      dp_actual_bw_allot_dn[i]=dp_dn_bw_demand[i];

      dp_bw_demand_met_dn[i]=1;

    }

    aggr_bw_allotted_up+=dp_actual_bw_allot_up[i];

    aggr_bw_allotted_dn+=dp_actual_bw_allot_dn[i];


    printf("DP %d: UP B/W: %lf (demand met=%d), DN B/W: %lf (demand met = %d)\n",i,dp_actual_bw_allot_up[i],dp_bw_demand_met_up[i],dp_actual_bw_allot_dn[i],dp_bw_demand_met_dn[i]);
  }

  bw_remain_up_after_first_pass = up_bw - aggr_bw_allotted_up;

  bw_remain_dn_after_first_pass = up_bw - aggr_bw_allotted_dn;

  printf("B/W allotted after first pass: %lf up, %lf dn\n",aggr_bw_allotted_up,aggr_bw_allotted_dn);

  printf("B/W remaining to allot in second pass: %lf up, %lf dn\n",bw_remain_up_after_first_pass,bw_remain_dn_after_first_pass);

  printf("Raw B/W fractions remaining (%lf up, %lf dn)\n",raw_bw_allot_remain_after_first_pass_up,raw_bw_allot_remain_after_first_pass_dn);

  //

  //

  printf("-----Final Pass B/W allocation to DP ports-----\n");

  for(i=0;i<num_dp;i++){

    if(dp_bw_demand_met_up[i]==0){

      dp_actual_bw_allot_up[i]=raw_bw_allot[i]*bw_remain_up_after_first_pass/raw_bw_allot_remain_after_first_pass_up;

      aggr_bw_allotted_up+=dp_actual_bw_allot_up[i];

    }

    if(dp_bw_demand_met_dn[i]==0){

      dp_actual_bw_allot_dn[i]=raw_bw_allot[i]*bw_remain_dn_after_first_pass/raw_bw_allot_remain_after_first_pass_dn;

      aggr_bw_allotted_dn+=dp_actual_bw_allot_dn[i];

    }

   

    if((dp_width[i]*dp_freq[i])>0){

      dp_util_up[i]=dp_actual_bw_allot_up[i]/(double)(dp_width[i]*dp_freq[i]);

      dp_util_dn[i]=dp_actual_bw_allot_dn[i]/(double)(dp_width[i]*dp_freq[i]);

      if(dp_util_up[i]>dp_util_dn[i]) dp_util_link_U2U3[i]=dp_util_up[i];

      else dp_util_link_U2U3[i]=dp_util_dn[i];

      if(ASYM_POWER_ON == 1) dp_util_link_U1[i]=(dp_util_up[i] + dp_util_dn[i])/ 2.0;

      else if(dp_util_up[i]>dp_util_dn[i]) dp_util_link_U1[i]=dp_util_up[i];

      else dp_util_link_U1[i]=dp_util_dn[i];     

    }

    else{ // This is the USB1 and USB2 simplification

      dp_util_up[i]=dp_util_dn[i]=dp_util_link_U2U3[i]=dp_util_link_U1[i]=1;

    }

   

    printf("DP Port: %d, UP B/W: %lf (util: %lf), DN B/W: %lf (util: %lf), Link Util U1: %lf, Link Util U2U3 = %lf\n",i,dp_actual_bw_allot_up[i],dp_util_up[i],dp_actual_bw_allot_dn[i], dp_util_dn[i],dp_util_link_U1[i],dp_util_link_U2U3[i]);

  }

  up_util_up = aggr_bw_allotted_up/up_bw;

  up_util_dn = aggr_bw_allotted_dn/up_bw;

  if(up_util_up > up_util_dn) up_util_link_U2U3=up_util_up;

  else up_util_link_U2U3=up_util_dn;

  if(ASYM_POWER_ON == 1) up_util_link_U1 = (up_util_up + up_util_dn)/ 2.0;

  else if(up_util_up > up_util_dn) up_util_link_U1=up_util_up;

  else up_util_link_U1=up_util_dn;

 

  printf("Aggr B/w Allotted: (%lf up, %lf dn)\n",aggr_bw_allotted_up,aggr_bw_allotted_dn);

  printf("Hub UP utilization: up: %lf, dn: %lf, link (U2U3): %lf, link(U1)\n",up_util_up,up_util_dn,up_util_link_U2U3,up_util_link_U1);

  //

 

  printf("----------------------------------------------\n");

  printf("Power Savings with U2 (Entry L1: %lf us, Exit L1: %lf us\n",USB3_ENTRY_U2,USB3_EXIT_U2);

  printf("----------------------------------------------\n");

  for(j=0;j<10;j++){

    time_tot = (double)(j+1)*125.0;


    printf("------- Power Savings with %lf us scheduling interval ---\n",time_tot);

    total_power_savings_hub_u1 = 0;

    total_power_savings_hub_u2 = 0;

    total_power_savings_hub_u3 = 0;


    time_u0_or_entry_exit_u1=(up_util_link_U1*time_tot)+USB3_ENTRY_U1+USB3_EXIT_U1;

    time_u0_or_entry_exit_u2=(up_util_link_U2U3*time_tot)+USB3_ENTRY_U2+USB3_EXIT_U2;

    time_u0_or_entry_exit_u3=(up_util_link_U2U3*time_tot)+USB3_ENTRY_U3+USB3_EXIT_U3;

 

    if(time_u0_or_entry_exit_u1 < time_tot){

      frac_u1 = 1.0-(time_u0_or_entry_exit_u1/time_tot);

      power_savings_port_u1 = frac_u1*(USB3_POWER_U0-USB3_POWER_U1);

      total_power_savings_hub_u1 += power_savings_port_u1;

    }

    else{

      frac_u1=0;

      power_savings_port_u1=0;

    }


    if(time_u0_or_entry_exit_u2 < time_tot){

      frac_u2 = 1.0-(time_u0_or_entry_exit_u2/time_tot);

      power_savings_port_u2 = frac_u2*(USB3_POWER_U0-USB3_POWER_U2);

      total_power_savings_hub_u2 += power_savings_port_u2;

    }

    else{

      frac_u2=0;

      power_savings_port_u2=0;

    }

    //

    if(time_u0_or_entry_exit_u3 < time_tot){

      frac_u3 = 1.0-(time_u0_or_entry_exit_u3/time_tot);

      power_savings_port_u3 = frac_u3*(USB3_POWER_U0-USB3_POWER_U3);

      total_power_savings_hub_u3 += power_savings_port_u3;

    }

    else{

      frac_u3=0;

      power_savings_port_u3=0;

    }

    //

    // printf("Hub UP: Util U1 %lf: U1: (frac: %lf, Power Savings U1: %lf), Util U2U3: %lf, U2: (frac: %lf, Power Savings U2: %lf), U3: (frac: %lf, Power Savings U3: %lf)\n",up_util_link_U1,frac_u1,power_savings_port_u1,up_util_link_U2U3,frac_u2,power_savings_port_u2,frac_u3,power_savings_port_u3);

    //

    for(i=0;i<num_dp;i++){

      time_u0_or_entry_exit_u1=(dp_util_link_U1[i]*time_tot)+USB3_ENTRY_U1+USB3_EXIT_U1;     

      time_u0_or_entry_exit_u2=(dp_util_link_U2U3[i]*time_tot)+USB3_ENTRY_U2+USB3_EXIT_U2;     

      time_u0_or_entry_exit_u3=(dp_util_link_U2U3[i]*time_tot)+USB3_ENTRY_U3+USB3_EXIT_U3;     

      frac_u1 = 1.0-(time_u0_or_entry_exit_u1/time_tot);

      if(frac_u1<0) frac_u1=0;

      frac_u2 = 1.0-(time_u0_or_entry_exit_u2/time_tot);

      if(frac_u2<0) frac_u2=0;

      frac_u3 = 1.0-(time_u0_or_entry_exit_u3/time_tot);

      if(frac_u3<0) frac_u3=0;

      //

      power_savings_port_u1 = frac_u1*(USB3_POWER_U0-USB3_POWER_U1);

      power_savings_port_u2 = frac_u2*(USB3_POWER_U0-USB3_POWER_U2);

      power_savings_port_u3 = frac_u3*(USB3_POWER_U0-USB3_POWER_U3);

      //

      total_power_savings_hub_u1 += power_savings_port_u1;

      total_power_savings_hub_u2 += power_savings_port_u2;

      total_power_savings_hub_u3 += power_savings_port_u3;

      //

      // printf("Hub DP:%d:  Util U1 %lf: U1: (frac: %lf, Power Savings U1: %lf), Util (U2/U3): %lf, U2: (frac: %lf, Power Savings U2: %lf), U3: (frac: %lf, Power Savings U3: %lf),\n",i,dp_util_link_U1[i],frac_u1,power_savings_port_u1,dp_util_link_U2U3[i],frac_u2,power_savings_port_u2,frac_u3,power_savings_port_u3);
}
    printf("Total HUB power savings: U1: %lf, U2: %lf, U3: %lf\n",total_power_savings_hub_u1,total_power_savings_hub_u2,total_power_savings_hub_u3);
  }
 
  printf("-------------\n");
  // -------- Runtime Measurement --------
  gettimeofday(&end_time, NULL);

  double runtime_us = 
      (end_time.tv_sec - start_time.tv_sec) * 1e6 +
      (end_time.tv_usec - start_time.tv_usec);

  double runtime_div10 = runtime_us / 10.0;

  struct utsname sysinfo;
  uname(&sysinfo);

  printf("----------------------------------------------\n");
  printf("Program Runtime: %lf microseconds (avg/10 = %lf us)\n",
         runtime_us, runtime_div10);
  printf("System Info: %s %s on %s\n",
         sysinfo.sysname,
         sysinfo.release,
         sysinfo.machine);
}



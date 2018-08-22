# from thingsboard_api_tools import TbApi         # pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade

import pandas as pd         # pip install pandas
import matplotlib.pyplot as plt
import numpy as np
import json
import sklearn
from sklearn import preprocessing
from sklearn import metrics
import re

import importlib.util
spec = importlib.util.spec_from_file_location("thingsboard_api_tools", "C:/dev/thingsboard_api_tools/thingsboard_api_tools/__init__.py")
foo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(foo)


import birdhouse_utils

from config import motherShipUrl, username, password, dashboard_template_name

tbapi = foo.TbApi(motherShipUrl, username, password)

method1 = []
method2 = []


# May 2 - 25
# calibrationData={"periodStart":1525287600000, "periodEnd":1527213018000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}}
# calibrationData={"periodStart":1525287600000, "periodEnd":1525978800000, "reference":"DEQ"}   # Wednesday, May 2, 2018 12:00:00 PM - Thursday, May 10, 2018 12:00:00 
# birdhouse 002

calibrationData = {}
# May 3 6PM - May 30 3PM
calibrationData[100] = {"periodStart":1532822400000, "periodEnd":1534235400000, "referenceDevice":"average(3,4,11,12,13)", "attributes":{"plantowerPM25conc":"pm25"}, "duration": "10m"} 
calibrationData[101] = {"periodStart":1532822400000, "periodEnd":1534442400000, "referenceDevice":"average(3,4,11,12,13)", "attributes":{"plantowerPM25conc":"pm25"}, "duration": "10m"} 
calibrationData[102] = {"periodStart":1532822400000, "periodEnd":1534442400000, "referenceDevice":"average(3,4,11,12,13)", "attributes":{"plantowerPM25conc":"pm25"}, "duration": "10m"} 

calibrationData[2] = {"periodStart":1525287600000, "periodEnd":1527717600000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}, "duration":"1H"}
calibrationData[3] = {"periodStart":1525287600000, "periodEnd":1527717600000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}, "duration":"1H"}
calibrationData[4] = {"periodStart":1525287600000, "periodEnd":1527717600000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}, "duration":"1H"}
calibrationData[5] = {"periodStart":1525287600000, "periodEnd":1527717600000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}, "duration":"1H"}
calibrationData[10] = {"periodStart":1525287600000, "periodEnd":1527717600000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}, "duration":"1H"}
calibrationData[11] = {"periodStart":1525287600000, "periodEnd":1527717600000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}, "duration":"1H"}
calibrationData[12] = {"periodStart":1525287600000, "periodEnd":1527717600000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}, "duration":"1H"}
calibrationData[13] = {"periodStart":1525287600000, "periodEnd":1527717600000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}, "duration":"1H"}

# 5/31/18 4PM - 6/18/18 12:00PM
calibrationData[6] = {"periodStart":1527807600000, "periodEnd":1529348400000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}, "duration":"1H"}
calibrationData[7] = {"periodStart":1527807600000, "periodEnd":1529348400000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}, "duration":"1H"}
calibrationData[9] = {"periodStart":1527807600000, "periodEnd":1529348400000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}, "duration":"1H"}
calibrationData[18] = {"periodStart":1527807600000, "periodEnd":1529348400000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}, "duration":"1H"}
calibrationData[20] = {"periodStart":1527807600000, "periodEnd":1529348400000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}, "duration":"1H"}
calibrationData[21] = {"periodStart":1527807600000, "periodEnd":1529348400000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}, "duration":"1H"}
calibrationData[24] = {"periodStart":1527807600000, "periodEnd":1529348400000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}, "duration":"1H"}
calibrationData[39] = {"periodStart":1527807600000, "periodEnd":1529348400000, "referenceDevice":"DEQ (SEL)", "attributes":{"plantowerPM25conc":"pm25"}, "duration":"1H"}





def main():
    # corr = compare(["002", "003", "004", "005", "010", "011", "012", "013"], '1h')
    # print(corr.to_string())

    # corr = compare(["007", "039", "009", "006", "020", "021", "024", "018"], '1h')
    # print(corr.to_string())
    # exit()

    formatDataForGoogle(["004","005"])
    exit()
    

    # # batch 1
    doit("002")
    doit("003")
    doit("004")
    doit("005")
    doit("010")
    doit("011")
    doit("012")
    doit("013")

    # batch 2
    doit("007")
    doit("039")
    doit("009")
    doit("006")
    doit("020")
    doit("021")
    doit("024")
    doit("018")

    print(method1)
    print(method2)      # better



# def formatDataForGoogle(bh_list):

#     d1,r = retrieve_data(bh_list[0])
#     d2,r = retrieve_data(bh_list[1])

#     for v in (d1):
#         print(v['ts'],"\t",v['value'])
#     for v in (d2):
#         print(v['ts'],"\t\t",v['value'])



'''
Compute a correlation for a list birdhouses and the corresponding reference dataset (where appropriate)... should be in the same spot for this to be meaningful
'''
def compare(bh_list, sample_time):
    timeframe = pd.Timedelta(sample_time)
    

    d, r = retrieve_data(bh_list[0])

    if r is not None:
        ref = create_data_frame(r, "ref").resample(timeframe).mean()
        data = create_data_frame(d, bh_list[0]).resample(timeframe).mean()
        first_index = 1
    else:
        ref = create_data_frame(d, bh_list[0]).resample(timeframe).mean()
        data = create_data_frame(d, bh_list[1]).resample(timeframe).mean()
        first_index = 2

    merged = pd.merge(ref, data, left_index=True, right_index=True)

    for i in bh_list[first_index:]:
        d,r = retrieve_data(i)
        data = create_data_frame(d, i).resample(timeframe).mean()
        merged = pd.merge(merged, data, left_index=True, right_index=True)


    corr = merged.corr()

    return corr




    
def doit(birdhouse_number):
    telemetry, reference_telemetry = retrieve_data(birdhouse_number)
    data_df = create_data_frame(telemetry, "data") 
    ref_df  = create_data_frame(reference_telemetry, "ref")

    # Combine our telemetry with the reference values into a single dataframe; average it to hourly periods; kick any periods where we're missing data
    merged = pd.merge_ordered(data_df, ref_df, on='ts', how='outer')
    merged.set_index('ts', inplace=True)
    merged.index = pd.to_datetime(merged.index)
    resampled = merged.resample('1H').mean()
    resampled.dropna(inplace=True) 


    ### Here we have two columns of aligned data (generally hourly, but could be other intervals) with missing values stripped out

    # Plot our original datasets
    plt.plot(resampled["ref"].values, label="deq ref")
    # plt.plot(resampled["data"].values, label="pms data")

    scaling_factor_based_on_rescaled_data = compute_scaling_factor(resampled["data"], resampled["ref"])
    # print("Scaling factor (resampled data)", scaling_factor_based_on_rescaled_data)

    scaling_factor_based_on_orig_data = compute_scaling_factor(data_df["data"], ref_df["ref"])
    # print("Scaling factor (orig data)", scaling_factor_based_on_orig_data)



    d_resampled = resampled["data"].values.reshape(-1, 1)
    r_resampled = resampled["ref"] .values.reshape(-1, 1)


    # Apply the correction factor derived from two different resolutions of our data in order to see which creates a better fit
    # with our reference data
    d_resampled_rescaled = d_resampled * scaling_factor_based_on_rescaled_data
    d_orig_rescaled      = d_resampled * scaling_factor_based_on_orig_data


    offset_rescaled = compute_offset(d_resampled_rescaled, r_resampled)
    offset_orig     = compute_offset(d_orig_rescaled,      r_resampled)




    # These are two candidates; both represent hourly data points.  Which fits better?
    d_resampled_rescaled_offset = d_resampled_rescaled + offset_rescaled
    d_orig_rescaled_offset      = d_orig_rescaled + offset_orig


    error_resampled_method = sklearn.metrics.r2_score(r_resampled, d_resampled_rescaled_offset)
    method1.append(error_resampled_method)
    # print("r^2 for dataset", birdhouse_number, "using resampled data method", error_resampled_method)  

    error_orig_method = sklearn.metrics.r2_score(r_resampled, d_orig_rescaled_offset)
    method2.append(error_orig_method)

    device_name = birdhouse_utils.make_device_name(birdhouse_number)


    print("Calibration factor/offset for device " + device_name + ":", scaling_factor_based_on_orig_data, offset_orig)



    # resampled["data"] = resampled["data"] 
    # corr = resampled.corr()
    # corr2 = merged.corr()

    # print("birdhouse number:",birdhouse_number, "\n", corr, "\n",corr2)


    # f = open(r'c:\temp\dataset_'+birdhouse_number, 'w')
    # f.write("ref_val, data_val\n")
    # for v in range(len(r_resampled)):
    #     f.write(str(r_resampled[v][0],)+","+str(d_resampled_rescaled_offset[v][0])+"\n")
    # f.close()
    # print("r^2 for dataset", birdhouse_number, "using original resolution method", error_orig_method) 


    # plt.plot(d_resampled_rescaled_offset, label="data_offset_resampled " +str(error_resampled_method))
    # plt.plot(d_orig_rescaled_offset, label="data_offset_orig " +str(error_orig_method))


    # plt.legend()
    # plt.show()



def retrieve_data(birdhouse_number):
    device_name = birdhouse_utils.make_device_name(birdhouse_number)
    
    if len(calibrationData[birdhouse_number]["attributes"]) == 0:
        print("Nothing to compare!")
        exit()

    start_ts = calibrationData[birdhouse_number]["periodStart"]
    end_ts   = calibrationData[birdhouse_number]["periodEnd"]
    reference_device_name = calibrationData[birdhouse_number]["referenceDevice"]

    attributes = []
    reference_attributes = []

    for a1, a2 in calibrationData[birdhouse_number]["attributes"].items():
        attributes.append(a1)
        reference_attributes.append(a2)


    device = tbapi.get_device_by_name(device_name)
    if device is None:
        print("Could not find device " + device_name)
        exit()
    reference_device = tbapi.get_device_by_name(reference_device_name)
    if reference_device is None:
        print("Could not find reference device " + reference_device_name)
        exit()
        # 004 005 003 010 002 011 012 013
    # for i in range(0, len(attributes)):
    #     telemetry = get_telemetry(device, attributes[i], start_ts, end_ts)[attributes[i]]
    #     reference_telemetry = get_telemetry(reference_device, reference_attributes[i], start_ts, end_ts)[reference_attributes[i]]

    #     print(telemetry)
    #     print(reference_telemetry)


    from testData import telemetry, reference_telemetry

    return telemetry[birdhouse_number], reference_telemetry[birdhouse_number]



def create_data_frame(data, name):
    '''
    Creates a pandas timeseries data frame indexed and ordered by the timestamp column
    Will rename value column "value" (which is what the data is called when retrieved from Thingsboard) to the specified name
    '''
    df = (pd.read_json(json.dumps(data), orient="records", typ="frame", convert_dates=['ts'])
              .set_index('ts')
              .sort_values(by=['ts'])) 

    df.rename(columns={"value":name}, inplace=True)

    return df



'''
Compute scaling factor you can mulitply data_df by to best match ref_df
'''
def compute_scaling_factor(data_df, ref_df):

    # Now attempt to calculate some scaling factors
    data_df = data_df.values.reshape(-1, 1)
    ref_df = ref_df.values.reshape(-1, 1)

    # Scale our reference and data separately
    d_scaler = preprocessing.StandardScaler()
    d_scaler.fit_transform(data_df)


    r_scaler = preprocessing.StandardScaler()
    r_scaler.fit_transform(ref_df)

    return r_scaler.scale_ / d_scaler.scale_ 


def compute_offset(data_df, ref_df):

    # d_scaler = preprocessing.StandardScaler(with_std=False)
    d_scaler = preprocessing.RobustScaler()
    r_scaler = preprocessing.RobustScaler()

    d_scaler.fit(data_df)
    r_scaler.fit(ref_df)

    return r_scaler.center_ - d_scaler.center_



def get_telemetry(device, field, start_ts, end_ts):
    return tbapi.get_telemetry(device, field, start_ts, end_ts, interval=1000, limit=99999)
#self, device, telemetry_keys, startTime=None, endTime=None, interval=None, limit=None, agg=None):
main()



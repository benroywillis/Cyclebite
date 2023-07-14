import tensorflow as tf
from sklearn.decomposition import PCA
import argparse
import json
import numpy as np
import joblib

def getPigFeatureVector(PD):
    # dictionaries hold all columns of each respective SQL table
    pigD = dict.fromkeys(["typeVoid", "typeFloat", "typeInt", "typeArray", "typeVector", "typePointer", "addCount",
                            "subCount", "mulCount", "udivCount", "sdivCount", "uremCount", "sremCount", "shlCount", "lshrCount", "ashrCount", "andCount", "orCount",
                            "xorCount", "faddCount", "fsubCount", "fmulCount", "fdivCount", "fremCount", "extractelementCount", "insertelementCount",
                            "shufflevectorCount", "extractvalueCount", "insertvalueCount", "allocaCount", "loadCount", "storeCount", "fenceCount", "cmpxchgCount",
                            "atomicrmwCount", "getelementptrCount", "truncCount", "zextCount", "sextCount", "fptruncCount", "fpextCount", "fptouiCount", "fptosiCount",
                            "uitofpCount", "sitofpCount", "ptrtointCount", "inttoptrCount", "bitcastCount", "addrspacecastCount", "icmpCount", "fcmpCount", "phiCount",
                            "selectCount", "freezeCount", "callCount", "va_argCount", "landingpadCount", "catchpadCount", "cleanuppadCount", "retCount", "brCount",
                            "switchCount", "indirectbrCount", "invokeCount", "callbrCount", "resumeCount", "catchswitchCount", "cleanupretCount", "unreachableCount",
                            "fnegCount"], 0)
    for inst in PD.keys():
        if inst == "instructionCount":
            continue
        for column in pigD.keys():
            if column == "instructionCount":
                continue
            pigD[column] = float(PD[inst]) / float(PD["instructionCount"])
            break
    return list(pigD.values())

def getEPigFeatureVector(PD):
    epigD = dict.fromkeys(["fnegtypeInt", "fnegtypeFloat", "fnegtypeVoid", "fnegtypeArray", "fnegtypeVector", "fnegtypePointer", "addtypeInt",
                            "addtypeFloat", "addtypeVoid", "addtypeArray", "addtypeVector", "addtypePointer", "faddtypeInt", "faddtypeFloat", "faddtypeVoid", "faddtypeArray",
                            "faddtypeVector", "faddtypePointer", "subtypeInt", "subtypeFloat", "subtypeVoid", "subtypeArray", "subtypeVector", "subtypePointer", "fsubtypeInt",
                            "fsubtypeFloat", "fsubtypeVoid", "fsubtypeArray", "fsubtypeVector", "fsubtypePointer", "multypeInt", "multypeFloat", "multypeVoid", "multypeArray",
                            "multypeVector", "multypePointer", "fmultypeInt", "fmultypeFloat", "fmultypeVoid", "fmultypeArray", "fmultypeVector", "fmultypePointer", "udivtypeInt",
                            "udivtypeFloat", "udivtypeVoid", "udivtypeArray", "udivtypeVector", "udivtypePointer", "sdivtypeInt", "sdivtypeFloat", "sdivtypeVoid", "sdivtypeArray",
                            "sdivtypeVector", "sdivtypePointer", "fdivtypeInt", "fdivtypeFloat", "fdivtypeVoid", "fdivtypeArray", "fdivtypeVector", "fdivtypePointer", "uremtypeInt",
                            "uremtypeFloat", "uremtypeVoid", "uremtypeArray", "uremtypeVector", "uremtypePointer", "sremtypeInt", "sremtypeFloat", "sremtypeVoid", "sremtypeArray",
                            "sremtypeVector", "sremtypePointer", "fremtypeInt", "fremtypeFloat", "fremtypeVoid", "fremtypeArray", "fremtypeVector", "fremtypePointer", "shltypeInt",
                            "shltypeFloat", "shltypeVoid", "shltypeArray", "shltypeVector", "shltypePointer", "lshrtypeInt", "lshrtypeFloat", "lshrtypeVoid", "lshrtypeArray", "lshrtypeVector",
                            "lshrtypePointer", "ashrtypeInt", "ashrtypeFloat", "ashrtypeVoid", "ashrtypeArray", "ashrtypeVector", "ashrtypePointer", "andtypeInt", "andtypeFloat", "andtypeVoid",
                            "andtypeArray", "andtypeVector", "andtypePointer", "ortypeInt", "ortypeFloat", "ortypeVoid", "ortypeArray", "ortypeVector", "ortypePointer", "xortypeInt", "xortypeFloat",
                            "xortypeVoid", "xortypeArray", "xortypeVector", "xortypePointer", "insertelementtypeInt", "insertelementtypeFloat", "insertelementtypeVoid", "insertelementtypeArray",
                            "insertelementtypeVector", "insertelementtypePointer", "shufflevectortypeInt", "shufflevectortypeFloat", "shufflevectortypeVoid", "shufflevectortypeArray", "shufflevectortypeVector",
                            "shufflevectortypePointer", "storetypeInt", "storetypeFloat", "storetypeVoid", "storetypeArray", "storetypeVector", "storetypePointer", "fencetypeInt", "fencetypeFloat", "fencetypeVoid",
                            "fencetypeArray", "fencetypeVector", "fencetypePointer", "cmpxchgtypeInt", "cmpxchgtypeFloat", "cmpxchgtypeVoid", "cmpxchgtypeArray", "cmpxchgtypeVector", "cmpxchgtypePointer",
                            "allocatypeInt", "allocatypeFloat", "allocatypeVoid", "allocatypeArray", "allocatypeVector", "allocatypePointer", "atomicrmwtypeInt", "atomicrmwtypeFloat", "atomicrmwtypeVoid",
                            "atomicrmwtypeArray", "atomicrmwtypeVector", "atomicrmwtypePointer", "getelementptrtypeInt", "getelementptrtypeFloat", "getelementptrtypeVoid", "getelementptrtypeArray",
                            "getelementptrtypeVector", "getelementptrtypePointer", "trunctypeInt", "trunctypeFloat", "trunctypeVoid", "trunctypeArray", "trunctypeVector", "trunctypePointer", "zexttypeInt",
                            "zexttypeFloat", "zexttypeVoid", "zexttypeArray", "zexttypeVector", "zexttypePointer", "sexttypeInt", "sexttypeFloat", "sexttypeVoid", "sexttypeArray", "sexttypeVector",
                            "sexttypePointer", "fptrunctypeInt", "fptrunctypeFloat", "fptrunctypeVoid", "fptrunctypeArray", "fptrunctypeVector", "fptrunctypePointer", "fpexttypeInt", "fpexttypeFloat",
                            "fpexttypeVoid", "fpexttypeArray", "fpexttypeVector", "fpexttypePointer", "fptouitypeInt", "fptouitypeFloat", "fptouitypeVoid", "fptouitypeArray", "fptouitypeVector",
                            "fptouitypePointer", "fptositypeInt", "fptositypeFloat", "fptositypeVoid", "fptositypeArray", "fptositypeVector", "fptositypePointer", "uitofptypeInt", "uitofptypeFloat",
                            "uitofptypeVoid", "uitofptypeArray", "uitofptypeVector", "uitofptypePointer", "sitofptypeInt", "sitofptypeFloat", "sitofptypeVoid", "sitofptypeArray", "sitofptypeVector",
                            "sitofptypePointer", "ptrtointtypeInt", "ptrtointtypeFloat", "ptrtointtypeVoid", "ptrtointtypeArray", "ptrtointtypeVector", "ptrtointtypePointer", "inttoptrtypeInt", "inttoptrtypeFloat",
                            "inttoptrtypeVoid", "inttoptrtypeArray", "inttoptrtypeVector", "inttoptrtypePointer", "bitcasttypeInt", "bitcasttypeFloat", "bitcasttypeVoid", "bitcasttypeArray", "bitcasttypeVector",
                            "bitcasttypePointer", "addrspacecasttypeInt", "addrspacecasttypeFloat", "addrspacecasttypeVoid", "addrspacecasttypeArray", "addrspacecasttypeVector", "addrspacecasttypePointer",
                            "icmptypeInt", "icmptypeFloat", "icmptypeVoid", "icmptypeArray", "icmptypeVector", "icmptypePointer", "fcmptypeInt", "fcmptypeFloat", "fcmptypeVoid", "fcmptypeArray", "fcmptypeVector",
                            "fcmptypePointer", "rettypeInt", "rettypeFloat", "rettypeVoid", "rettypeArray", "rettypeVector", "rettypePointer", "brtypeInt", "brtypeFloat", "brtypeVoid", "brtypeArray",
                            "brtypeVector", "brtypePointer", "indirectbrtypeInt", "indirectbrtypeFloat", "indirectbrtypeVoid", "indirectbrtypeArray", "indirectbrtypeVector", "indirectbrtypePointer", "switchtypeInt",
                            "switchtypeFloat", "switchtypeVoid", "switchtypeArray", "switchtypeVector", "switchtypePointer", "invoketypeInt", "invoketypeFloat", "invoketypeVoid", "invoketypeArray", "invoketypeVector",
                            "invoketypePointer", "callbrtypeInt", "callbrtypeFloat", "callbrtypeVoid", "callbrtypeArray", "callbrtypeVector", "callbrtypePointer", "resumetypeInt", "resumetypeFloat", "resumetypeVoid",
                            "resumetypeArray", "resumetypeVector", "resumetypePointer", "catchrettypeInt", "catchrettypeFloat", "catchrettypeVoid", "catchrettypeArray", "catchrettypeVector",
                            "catchrettypePointer", "cleanuprettypeInt", "cleanuprettypeFloat", "cleanuprettypeVoid", "cleanuprettypeArray", "cleanuprettypeVector", "cleanuprettypePointer",
                            "unreachabletypeInt", "unreachabletypeFloat", "unreachabletypeVoid", "unreachabletypeArray", "unreachabletypeVector", "unreachabletypePointer", "extractelementtypeInt",
                            "extractelementtypeFloat", "extractelementtypeVoid", "extractelementtypeArray", "extractelementtypeVector", "extractelementtypePointer", "extractvaluetypeInt",
                            "extractvaluetypeFloat", "extractvaluetypeVoid", "extractvaluetypeArray", "extractvaluetypeVector", "extractvaluetypePointer", "insertvaluetypeInt", "insertvaluetypeFloat",
                            "insertvaluetypeVoid", "insertvaluetypeArray", "insertvaluetypeVector", "insertvaluetypePointer", "selecttypeInt", "selecttypeFloat", "selecttypeVoid", "selecttypeArray",
                            "selecttypeVector", "selecttypePointer", "loadtypeInt", "loadtypeFloat", "loadtypeVoid", "loadtypeArray", "loadtypeVector", "loadtypePointer", "phitypeInt", "phitypeFloat",
                            "phitypeVoid", "phitypeArray", "phitypeVector", "phitypePointer", "freezetypeInt", "freezetypeFloat", "freezetypeVoid", "freezetypeArray", "freezetypeVector", "freezetypePointer",
                            "calltypeInt", "calltypeFloat", "calltypeVoid", "calltypeArray", "calltypeVector", "calltypePointer", "va_argtypeInt", "va_argtypeFloat", "va_argtypeVoid", "va_argtypeArray",
                            "va_argtypeVector", "va_argtypePointer", "landingpadtypeInt", "landingpadtypeFloat", "landingpadtypeVoid", "landingpadtypeArray", "landingpadtypeVector", "landingpadtypePointer",
                            "catchpadtypeInt", "catchpadtypeFloat", "catchpadtypeVoid", "catchpadtypeArray", "catchpadtypeVector", "catchpadtypePointer", "cleanuppadtypeInt", "cleanuppadtypeFloat",
                            "cleanuppadtypeVoid", "cleanuppadtypeArray", "cleanuppadtypeVector", "cleanuppadtypePointer", "catchswitchtypeVector", "catchswitchtypePointer","catchswitchtypeInt","catchswitchtypeFloat",
                            "catchswitchtypeVoid","catchswitchtypeArray"], 0)
    for inst in PD.keys():
        if inst == "instructionCount":
            continue
        for column in epigD.keys():
            if column == "instructionCount":
                continue
            if inst == column:
                epigD[column] = float(PD[inst]) / float(PD["instructionCount"])
                break
    return list(epigD.values())

def argument_parse():
    arg_parser = argparse.ArgumentParser(description="Predicts the labels found in the input kernel file")
    arg_parser.add_argument("-kf","--kernel-file", default="kernel.json", help="Specify path to input dataset csv file.")
    arg_parser.add_argument("-m","--model", default="model.joblib", help="Specify path to data preprocessing model file.")
    arg_parser.add_argument("-d","--dnn", default="DNN.h5", help="Specify path to DNN file.")
    return arg_parser.parse_args()

def processLabel(label, labelMap):
    # needs to be truncated (just the first one with no modifiers) and possibly mapped to an abbreviation
    doNothing = True

def readParameterFile(args):
    labelMappings = {}
    dataPrep = joblib.load(args.model)
    LBinarizer = dataPrep["Binarizer"]
    PCA        = dataPrep["PCA"]
    return LBinarizer, PCA

def readKernelFile(args):
    kf = {}
    with open(args.kernel_file, "r") as f:
        kf = json.load( f )
        kernels = kf["Kernels"]
    dataMap = {}
    # when constructing this data map, all the features have to be in the same order as they are when they are pulled from the database (that was when all these models were trained). 
    # See DashAutomate/SQL.py to see how these numbers are pushed to the database
    # See OntologyML/Datasets/GetDataNew.py ReadData() to see how these are pulled from the database
    for index in kernels:
        pi   = getPigFeatureVector(kernels[index]["Performance Intrinsics"]["Pig"])
        cpi  = getPigFeatureVector(kernels[index]["Performance Intrinsics"]["CPig"])
        epi  = getEPigFeatureVector(kernels[index]["Performance Intrinsics"]["EPig"])
        ecpi = getEPigFeatureVector(kernels[index]["Performance Intrinsics"]["ECPig"])
        dataMap[index] = {}
        dataMap[index]["Feature Vector"] = pi+cpi+epi+ecpi
        dataMap[index]["Label"] = kernels[index]["Labels"]
    return dataMap

def dataPreprocessing(dataMap, pca, args):
    # turn the input data map into an array
    dataMatrix = []
    for index in dataMap:
        dataMatrix.append(dataMap[index]["Feature Vector"])
    # preprocesses the input feature vectors to the dimension of the model
    reducedMatrix = pca.transform(dataMatrix)
    i = 0
    for index in dataMap:
        dataMap[index]["Feature Vector"] = reducedMatrix[i]
        i += 1
    return dataMap

def RunModel(dataMap, labelMap, LBinarizer, args):
    # import the model
    model = tf.keras.models.load_model(args.dnn)
    predictionMap = dict.fromkeys(dataMap.keys())
    for index in dataMap:
        # figure out the input shape of the sample that will fit with the given model
        inShape = list(model.layers[0].input_shape)
        for i in range(len(inShape)):
            if inShape[i] == None:
                inShape[i] = 1
        # construct the sample
        sample = np.ndarray(shape=tuple(inShape), buffer=np.array(dataMap[index]["Feature Vector"]) )
        # predict what it is and record
        y_pred = model.predict(sample)
        predictionMap[index] = LBinarizer.classes_[np.argmax(y_pred, axis=1)][0]
    kf = {}
    with open(args.kernel_file, "r") as f:
        kf = json.load(f)
    for index in predictionMap:
        kf["Kernels"][index]["Predicted Label"] = predictionMap[index]
    with open(args.kernel_file, "w") as f:
        json.dump(kf, f, indent=4)

# script
args = argument_parse()
LBinarizer, PCA = readParameterFile(args)
dataMap = readKernelFile(args)
dataMap = dataPreprocessing(dataMap, PCA, args)
RunModel(dataMap, {}, LBinarizer, args)
#!/usr/bin/env python3

# MHD linear modes convergence plots
import os,sys
import glob
import numpy as np
import matplotlib.pyplot as plt

import pyHARM
import pyHARM.ana.plot as hplt

for dumpname in np.sort(glob.glob("bz_monopole.out0.000*0.phdf")):
    dump = pyHARM.load_dump(dumpname)
    fig, ax = plt.subplots(1,1,figsize=(7,7))
    hplt.plot_xz(ax, dump, 'rho')
    hplt.overlay_field(ax, dump)
    plt.savefig(dumpname+".png")
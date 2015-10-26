# -*- coding:utf-8 -*-
# Copyright (c) 2015, Galaxy Authors. All Rights Reserved
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Author: wangtaize@baidu.com
# Date: 2015-04-06
import datetime
import logging
from sofa.pbrpc import client
from galaxy import master_pb2

LOG = logging.getLogger('console')
class BaseEntity(object):
    def __setattr__(self,name,value):
        self.__dict__[name] = value
    def __getattr__(self,name):
        return self.__dict__.get(name,None)

class GalaxySDK(object):
    """
    Lumia python sdk
    """
    def __init__(self, master_addr):
        self.channel = client.Channel(master_addr)

    def get_agents(self):
        """
        """
        master = master_pb2.Master_Stub(self.channel)

    

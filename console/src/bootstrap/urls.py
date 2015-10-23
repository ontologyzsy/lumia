# -*- coding:utf-8 -*-
from django.conf import urls
from django.contrib import admin
urlpatterns = urls.patterns('bootstrap.views',
     (r'^$', 'index'),
)



#urlpatterns += urls.patterns('',
#     (r'^quota/', urls.include('console.quota.urls')),
#)


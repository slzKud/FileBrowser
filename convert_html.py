import base64
import os
if not os.path.exists("./index.html"):
    print("index.html not found. Unable to convert.")
else:
    fi_cc=open("./html.cc","w")
    fi_html=open("./index.html","r")
    html_b64=base64.b64encode(bytes(fi_html.read(),"utf-8"))
    cc_b64_tempale='''std::string FRONTEND_HTML_BASE64 = "%s";'''
    cc_b64=cc_b64_tempale%(html_b64.decode("utf-8"))
    fi_cc.write(cc_b64)
    fi_html.close()
    fi_cc.close()
    print("Convert index.html to html.cc ok.")

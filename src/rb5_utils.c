/*
 * rb5_utils.c
 *
 * Author: Peter Rodriguez 2016-Sep-08
 * 
 * 2016-09-08:  PR  new subroutines,
 *                  - read_xml_header_into_buffer()
 *                  - return_xpath_blobid_raw()
 *                  - convert_raw_to_data()
 *                  - what_is_this_param_to_urp()
 * 2016-09-09:  PR  new subroutines,
 *                  - get_rb5_file_info()
 *                  - get_rb5_param_info()

 * 2016-09-10:  PR  Move towards using strRB5_PARAM_INFO in subroutines
 *                  Ensure pointer use is within args not at subroutine return value
 *                  (lesson: it's bad practice, if that pointer was reused after another function call,
 *                   it'll reuse that memory in the stack, the behavior of the program becomes undefined)
 *                  new/redone subroutines,
 *                  - uncompress_this_blob()
 *                  - get_blobid_buffer()
 *                  - convert_raw_to_data()
 *                  - return_param_blobid_raw()
 *                  - find_in_rayinfo_name_arr()
 *                  - find_in_rawdata_name_arr()
 *                  - dump_strRB5_PARAM_INFO()
 * 2016-09-13:  PR  Fix reorder_by_iray_0degN() error: wrong pre-0 output_ index. Now [(n-p)+i]
 *                  Replaced calc_iray_0degN(rb5_param) with get_iray_0degN(req_slice)
 * 2016-09-14:  PR  Add iray_0degN to get_rb5_param_info()
 *                  Move reorder_by_iray_0degN() from main() to return_param_blobid_raw()
 * 2016-09-19:  PR  Move func_iso8601_2_urpvalid() to time_utils.c
 * 2016-09-20:  PR  Add get_xpath_iso8601_attrib()
 *                  Fix rb5_param.iso8601 to get it from parent <slicedata>
 * 2016-09-21:  PR  Add to strRB5_INFO: inp_file_basename, inp_file_dirname, inp_file_data_type
 * 2016-09-22:  PR  Add close_rb5_info() and clean up return(EXIT_SUCCESS|EXIT_FAILURE|EXIT_NULL_VAL)
 * 2016-10-24:  PR  Add map_rb5_to_h5_param()
 * 2016-10-25:  PR  One big buffer read then parse
 *                  - read_rb5_file_2_buffer() renamed and works solo
 *                  - populate_rb5_info() renamed
 *                  - close_rb5_info() renamed
 *                  - get_blobid_buffer() works from buffer instead of fp
 * 2016-10-27:  PR  data_arr is now (void *)
 *                  - return_param_blobid_raw() reworked
 *                  - convert_raw_to_data() reworked
 *                  - reorder_by_iray_0degN() reworked
 *                  - get_iray_0degN(), get_slice_end_iso8601() and rb5_probe.c using "void *raw_arr=NULL;"
 * 2016-10-28:  PR  clean up rb5_utils.h #define
 *                  make generic find_in_string_arr(char arr[][MAX_NSTRINGS],...)
 *
 * 2016-11-24:  PR  Added return_xpath_name() to query xpath existence
 *                  Added xml_block, sensor & history to rb5_info structure
 *                  Updated populate_rb5_info(),
 *                  - new L_VERBOSE option
 *                  - fixed filename extension and sparam extraction
 *                  - checks for rawfile and Rainbow version (for use by is_rb5.c)
 *                  - populates xml_block, sensorinfo & history blocks
 *
 * 2017-02-01: PR   Added nrays, nbins, iray_0degN per slice to strRB5_INFO
 *                  Renamed get_iray_0degN() to get_slice_iray_0degN()
 *                  - Check for RHI (ele), no re-ordering
 *                  New get_slice_dims_and_mid_angle_readbacks()
 *                  - add update of nrays, nbins, n_elems_data per slice
 *                  - assemble moving|fixed ray start|stop arrays
 *                  - calc midbeam readbacks
 *
 * 2017-02-02: PR   Rename to get_slice_mid_angle_readbacks()
 *                  - working with nrays instead of n_elems_data
 *                  All slice dims updated by 'dBZ' section in populate_rb5_info()
 *                  Simplify get_rb5_param_info() args with rb5_info
 *
 * 2017-02-23: PR   Update map_rb5_to_h5_param() to default to toupper(sPARAM)
 *
 * 2017-03-23: PR   Removed special param data range packing check (KDP's variable!)
 *
 * 2017-04-20: PR   Upgrade get_slice_mid_angle_readbacks()
 *                  Enforcing 1/1000 precision limit on ray readbacks
 *                  Add capture of all ray readbacks start and stop
 *                  Handles fallbacks and negative elevation angles
 *
 */

#include "rb5_utils.h"
#include "time_utils.h"

//#############################################################################

size_t uncompress_this_blob(unsigned char *buf, unsigned char** return_uncompressed_blob, size_t compressed_size_blob) {

    size_t expectedSize=(buf[0] << 24) |
                        (buf[1] << 16) |
                        (buf[2] <<  8) |
                        (buf[3]      );

    unsigned char *uncompressed_blob=(unsigned char *)malloc(expectedSize);

    int Z_result=uncompress(uncompressed_blob,&expectedSize,buf+4,compressed_size_blob-4);
    if (Z_result != Z_OK) {
      fprintf(stderr,"zlib error: %d\n", Z_result);
    }
    
    *return_uncompressed_blob=uncompressed_blob;
    return(expectedSize);
}

//#############################################################################

size_t get_xpath_size(const xmlXPathContextPtr xpathCtx, char *xpath){
    size_t return_NULL_val=0;

    const xmlChar* xpathExpr = (const xmlChar*) xpath;

    // evaluate xpath expression
    xmlXPathObjectPtr xpathObj = xmlXPathEvalExpression(xpathExpr, xpathCtx);
    if(xpathObj == NULL) {
        fprintf(stderr,"Error: unable to evaluate xpath expression \"%s\"\n", xpathExpr);
        return(return_NULL_val);
    }

    // use xpath object
    xmlNodeSetPtr nodes = xpathObj->nodesetval;
    size_t size = (nodes) ? nodes->nodeNr : 0;
    if(size == 0) {
        if(L_DEBUG_OUTPUT_1) fprintf(stderr,"Error: xpath expression not found \"%s\"\n", xpathExpr);
        xmlXPathFreeObject(xpathObj);
        return(return_NULL_val);
    }

    /* Cleanup */
    xmlXPathFreeObject(xpathObj);

    return(size);
}

//#############################################################################

char *return_xpath_name(const xmlXPathContextPtr xpathCtx, char *xpath){

    const xmlChar* xpathExpr = (const xmlChar*) xpath;

    // evaluate xpath expression
    xmlXPathObjectPtr xpathObj = xmlXPathEvalExpression(xpathExpr, xpathCtx);
    if(xpathObj == NULL) {
        fprintf(stderr,"Error: unable to evaluate xpath expression \"%s\"\n", xpathExpr);
        return("\0");
    }

    // use xpath object
    xmlNodeSetPtr nodes = xpathObj->nodesetval;
    size_t size = (nodes) ? nodes->nodeNr : 0;
    if(size == 0) {
        if(L_DEBUG_OUTPUT_1) fprintf(stderr,"Error: xpath expression not found \"%s\"\n", xpathExpr);
        xmlXPathFreeObject(xpathObj);
        return("\0");
    }

    //assume uniqueness, use [position()=X] in xpath 
    //having used get_xpath_size() to build looping code
    int iNode = 0; // get only 1st value at xpath
    xmlNodePtr cur = (xmlNodePtr)xpathObj->nodesetval->nodeTab[iNode];
    if(cur == NULL) {
        fprintf(stderr,"Error: NULL node at xpath expression \"%s\"\n", xpathExpr);
        xmlXPathFreeObject(xpathObj);
        return("\0");
    }
    char *value = (char *) cur->name;
//    fprintf(stdout,"XPATH: %s = %s\n", xpathExpr, value);

    /* Cleanup */
    xmlXPathFreeObject(xpathObj);

    return(value);
}

//#############################################################################

char *return_xpath_value(const xmlXPathContextPtr xpathCtx, char *xpath){

    const xmlChar* xpathExpr = (const xmlChar*) xpath;

    // evaluate xpath expression
    xmlXPathObjectPtr xpathObj = xmlXPathEvalExpression(xpathExpr, xpathCtx);
    if(xpathObj == NULL) {
        fprintf(stderr,"Error: unable to evaluate xpath expression \"%s\"\n", xpathExpr);
        return("\0");
    }

    // use xpath object
    xmlNodeSetPtr nodes = xpathObj->nodesetval;
    size_t size = (nodes) ? nodes->nodeNr : 0;
    if(size == 0) {
        if(L_DEBUG_OUTPUT_1) fprintf(stderr,"Error: xpath expression not found \"%s\"\n", xpathExpr);
        xmlXPathFreeObject(xpathObj);
        return("\0");
    }

    //assume uniqueness, use [position()=X] in xpath 
    //having used get_xpath_size() to build looping code
    int iNode = 0; // get only 1st value at xpath
    xmlNodePtr cur = (xmlNodePtr)xpathObj->nodesetval->nodeTab[iNode];
    if(cur == NULL) {
        fprintf(stderr,"Error: NULL node at xpath expression \"%s\"\n", xpathExpr);
        xmlXPathFreeObject(xpathObj);
        return("\0");
    }
    char *value = (char *) cur->children->content;
//    fprintf(stdout,"XPATH: %s = %s\n", xpathExpr, value);

    /* Cleanup */
    xmlXPathFreeObject(xpathObj);

    return(value);
}

//#############################################################################

char *get_xpath_iso8601_attrib(const xmlXPathContextPtr xpathCtx, char *xpath_bgn){
    // expects trailing "/" 

    char xpath[MAX_STRING]="\0";
    static char iso8601[MAX_STRING]="\0";
    
    if(get_xpath_size(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@datetimehighaccuracy")) == 1){
      strcpy(iso8601,return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@datetimehighaccuracy")));
      strncpy(iso8601+10," ",1); //blank T-delimiter
    } else {
      if(get_xpath_size(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@date")) ||
         get_xpath_size(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@time")) == 1) {
        sprintf(iso8601,"%s %s",
          return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@date")),
          return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@time")));
      }
    }
    //fprintf(stdout,"xpath = %s, iso8601=%s\n",xpath,iso8601);
    return(iso8601);

}

//#############################################################################

size_t read_rb5_file_2_buffer(char *inp_fname, char **return_buffer){

    size_t EXIT_NULL_VAL=0;
    char *buffer=NULL;
    size_t buffer_len=0;

    FILE *fp = NULL;
    fp = fopen(inp_fname, "r");
    if (NULL == fp) {
        fprintf(stderr,"Error while opening file = %s\n", inp_fname);
        return(EXIT_NULL_VAL);
    }

    if (fseek(fp,0L,SEEK_END) == 0) {
        /* Get the size of the file. */
        buffer_len=ftell(fp);
        if (buffer_len == -1) {
            fprintf(stderr,"Error while reading file\n");
            return(EXIT_NULL_VAL);
        }

        /* Allocate our buffer to that size. */
        buffer=malloc(sizeof(char)*(buffer_len));
        if(L_DEBUG_OUTPUT_1) fprintf(stdout,"buffer_len = %ld\n",buffer_len);

        /* Go back to the start of the file. */
        if (fseek(fp,0L,SEEK_SET) != 0) {
            fprintf(stderr,"Error while reading file\n");
            return(EXIT_NULL_VAL);
        }

        /* Read the entire file into memory. */
        if (fread(buffer,sizeof(char),buffer_len,fp) <= 0) {
            fprintf(stderr,"Error while reading file\n");
            return(EXIT_NULL_VAL);
        }
    } //if (fseek(fp,0L,SEEK_END) == 0) {
    fclose(fp);

    *return_buffer=buffer;

    return(buffer_len);
}

//#############################################################################

size_t get_blobid_buffer(strRB5_INFO *rb5_info, int req_blobid, unsigned char** return_uncompressed_blob) {

    size_t EXIT_NULL_VAL=0;

    char *xpath;
    int this_blobid;
    unsigned char *compressed_blob = NULL;
    unsigned char *uncompressed_blob = NULL;
    size_t compressed_size_blob;
    size_t uncompressed_size_blob;

    char *blobspace=NULL;
    size_t bs_size=(rb5_info->buffer_len) - (rb5_info->byte_offset_blobspace);
//fprintf(stdout,"bs_size=%ld\n",bs_size);
    blobspace=(rb5_info->buffer) + (rb5_info->byte_offset_blobspace);
    size_t bs_abs_off=0;
    size_t bs_rel_jmp=0;

    char bgn_BLOB[]="<BLOB ";
    char *BLOB_line=NULL;
    size_t bgn_BLOB_len=0;
    char end_BLOB[]="</BLOB>";
    size_t end_BLOB_len=strlen(end_BLOB)+2; //with trailing '\n'

    while (bs_abs_off <= bs_size) {

      BLOB_line=strstr(strtok(blobspace,"\n"),bgn_BLOB);
      bgn_BLOB_len=strlen(BLOB_line)+1; //with trailing '\n'
//fprintf(stdout,"(%ld) BLOB_line=%s\n",strlen(BLOB_line),BLOB_line);
//fprintf(stdout,"(%ld) end_BLOB=%s\n",strlen(end_BLOB),end_BLOB);

      if(L_DEBUG_OUTPUT_1) fprintf(stdout,"%s\n", BLOB_line);

            // parse the BLOB header and get the DOM (</BLOB ...> only so force a quiet parse recovery)
            xmlDoc *blob_doc = xmlReadMemory(BLOB_line,bgn_BLOB_len, "noname.xml", NULL, XML_PARSE_RECOVER+XML_PARSE_NOERROR);
            if ( blob_doc == NULL ) {
                fprintf(stderr,"Error while parsing BLOB header\n");
                free(blobspace);
                xmlFreeDoc(blob_doc);
                return(EXIT_NULL_VAL);
            }
            xmlXPathContextPtr blob_xpathCtx = xmlXPathNewContext(blob_doc);
            if (blob_xpathCtx == NULL ){
                fprintf(stderr,"Error in xmlXPathNewContext\n");
                free(blobspace);
                xmlXPathFreeContext(blob_xpathCtx); //cleanup
                xmlFreeDoc(blob_doc); // free the document
                return(EXIT_NULL_VAL);
            }

            xpath="/BLOB/@size";
            compressed_size_blob=atoi(return_xpath_value(blob_xpathCtx,xpath));

            xpath="/BLOB/@blobid";
            //printf("XPATH: %s = %s\n", xpath, return_xpath_value(blob_xpathCtx,xpath));
            this_blobid=atoi(return_xpath_value(blob_xpathCtx,xpath));
            if (this_blobid == req_blobid) {
                if(L_DEBUG_OUTPUT_1) fprintf(stdout,"  compressed_size_blob = %ld\n",compressed_size_blob);
                compressed_blob=(unsigned char *)malloc(compressed_size_blob);
                memcpy(compressed_blob,blobspace+bgn_BLOB_len,compressed_size_blob);
                    uncompressed_size_blob=uncompress_this_blob(compressed_blob, &uncompressed_blob, compressed_size_blob);
                    if(L_DEBUG_OUTPUT_1) fprintf(stdout,"uncompressed_size_blob = %ld\n",uncompressed_size_blob);
                    free(compressed_blob);
                    xmlXPathFreeContext(blob_xpathCtx); //cleanup
                    xmlFreeDoc(blob_doc);
                    *return_uncompressed_blob=uncompressed_blob;
                    return(uncompressed_size_blob);
            } 
            bs_rel_jmp=bgn_BLOB_len+compressed_size_blob+end_BLOB_len;
            blobspace+=bs_rel_jmp;
            bs_abs_off+=bs_rel_jmp;

//fprintf(stdout,"bs_abs_off=%ld\n",bs_abs_off);
//fprintf(stdout,"bs_rel_jmp=%ld\n",bs_rel_jmp);

            xmlXPathFreeContext(blob_xpathCtx); //cleanup
            xmlFreeDoc(blob_doc);

    } //while (bs_abs_off <= bs_size) {

    fprintf(stdout,"ERROR: req_blobid = %d NOT FOUND!!!\n",req_blobid);
    return(EXIT_NULL_VAL);
}

//#############################################################################

void convert_raw_to_data(strRB5_PARAM_INFO *rb5_param, void **input_raw_arr, float **return_data_arr){

    //local vars
    char conversion[MAX_STRING]="\0";
    char sparam[MAX_STRING]="\0";
    strcpy(conversion,rb5_param->conversion);
    strcpy(sparam,rb5_param->sparam);
    size_t n_elems_data    =rb5_param->n_elems_data;
    size_t raw_binary_depth=rb5_param->raw_binary_depth;
    size_t raw_binary_width=rb5_param->raw_binary_width;
    float data_range_min   =rb5_param->data_range_min;
    float data_range_max   =rb5_param->data_range_max;
    float data_range_width =rb5_param->data_range_width;
    float data_step        =rb5_param->data_step;
    float NODATA_val=-99;

    size_t i;
    unsigned int *raw_arr=malloc(n_elems_data*sizeof(unsigned int));
    void *deref_input_raw_arr=*input_raw_arr;
           if (raw_binary_depth ==  8){
        uint8_t  *buffer_8 =((uint8_t  *)deref_input_raw_arr);
        for (i = 0; i < n_elems_data; i++) raw_arr[i]=(unsigned int)buffer_8 [i];
    } else if (raw_binary_depth == 16){
        uint16_t *buffer_16=((uint16_t *)deref_input_raw_arr);
        for (i = 0; i < n_elems_data; i++) raw_arr[i]=(unsigned int)buffer_16[i];
    } else if (raw_binary_depth == 32){
        uint32_t *buffer_32=((uint32_t *)deref_input_raw_arr);
        for (i = 0; i < n_elems_data; i++) raw_arr[i]=(unsigned int)buffer_32[i];
    }
//fprintf(stdout,"### (%2ld) param=%s\n",raw_binary_depth,sparam);
//for (i = 0; i < n_elems_data; i++) fprintf(stdout,"%d ",raw_arr[i]);
//fprintf(stdout,"\n");

    float *data_arr=NULL;
    data_arr=malloc(n_elems_data*sizeof(float));
    if (strcmp(conversion, "copy") == 0) {
        for (i = 0; i < n_elems_data; i++) {
          data_arr[i]=raw_arr[i];
          if(L_DEBUG_OUTPUT_2) fprintf(stdout,"%f ",data_arr[i]);
        }
        if(L_DEBUG_OUTPUT_2) fprintf(stdout,"\n");
        NODATA_val = 0;
    } else if (strcmp(conversion, "angular") == 0) {
        data_range_min=0.0;
        data_range_max=360.0;
        data_range_width=data_range_max-data_range_min;
        data_step=data_range_width/raw_binary_width;
        if(L_DEBUG_OUTPUT_1) fprintf(stdout,"  data_step = %f\n",data_step);
        for (i = 0; i < (n_elems_data); i++) {
          data_arr[i]=(raw_arr[i] * data_step) + data_range_min;
          if(L_DEBUG_OUTPUT_2) fprintf(stdout,"%d ",raw_arr[i]);
          if(L_DEBUG_OUTPUT_2) fprintf(stdout,"%f ",data_arr[i]);
        }
        if(L_DEBUG_OUTPUT_2) fprintf(stdout,"\n");
        NODATA_val = (0 * data_step) - data_step + data_range_min;
    } else if (strstr(conversion, "data") != NULL) {
        //removed special param packing check, 2017-Mar-23
        // we found KDP have variable data range!
        // using rb5_param->data_range_min|max defaults (set in var declaration above)
        //if (strcmp(conversion, "kdp_data") == 0) {
        //    data_range_min=-20.0;
        //    data_range_max=+20.0;
        //} else if (strcmp(conversion, "phidp_data") == 0) {
        //    data_range_min=  0.0;
        //    data_range_max=360.0;
        //}
        data_range_width=data_range_max-data_range_min;
        data_step=data_range_width/raw_binary_width;
        if(L_DEBUG_OUTPUT_1) fprintf(stdout,"  data_step = %f\n",data_step);
        for (i = 0; i < n_elems_data; i++) {
          data_arr[i]=(raw_arr[i] * data_step) - data_step + data_range_min;
          if(L_DEBUG_OUTPUT_2) fprintf(stdout,"%f ",data_arr[i]);
        }
        if(L_DEBUG_OUTPUT_2) fprintf(stdout,"\n");
        NODATA_val = (0 * data_step) - data_step + data_range_min;
    }
    if(L_DEBUG_OUTPUT_1) fprintf(stdout,"  NODATA_val = %f\n",NODATA_val);

    //update
    rb5_param->data_range_min  =data_range_min;
    rb5_param->data_range_max  =data_range_max;
    rb5_param->data_range_width=data_range_width;
    rb5_param->data_step       =data_step;
    rb5_param->NODATA_val      =NODATA_val;

    free(raw_arr); 
    *return_data_arr=data_arr;
}

//#############################################################################

size_t return_param_blobid_raw(strRB5_INFO *rb5_info, strRB5_PARAM_INFO* rb5_param, void **return_raw_arr){

    size_t EXIT_NULL_VAL=0;

    //local vars
    size_t blobid          =rb5_param->blobid;
    size_t size_blob;
    size_t n_elems_data;
    size_t data_bytesize   =rb5_param->data_bytesize;    
    size_t raw_binary_depth=rb5_param->raw_binary_depth;
    size_t i;
 
    unsigned char *blob_buffer=NULL;
    size_blob=get_blobid_buffer(&(*rb5_info), blobid, &blob_buffer);
    if (blob_buffer == NULL) {
      fprintf(stdout,"ERROR: blobid = %ld NOT FOUND!!!\n",blobid);
      xmlCleanupParser(); // free globals
      return(EXIT_NULL_VAL);
    }

    n_elems_data=size_blob/data_bytesize;
    void *raw_arr=(void *)malloc(size_blob);
    if (raw_binary_depth == 8 ) {
        /*8 bit data (copy blob_buffer)*/
        uint8_t *buffer_8=(uint8_t *)malloc(size_blob);
        memcpy(buffer_8 ,blob_buffer,size_blob);
        memcpy(raw_arr,buffer_8 ,size_blob);
        free(buffer_8);
    } else if (raw_binary_depth == 16) {
        /*16 bit data (put on Little Endian order)*/
        uint16_t *buffer_16=(uint16_t *)malloc(size_blob);
        memcpy(buffer_16,blob_buffer,size_blob);
        for (i = 0; i < n_elems_data; i++) {
            buffer_16[i]=(buffer_16[i] << 8) | (buffer_16[i] >> 8 );
        }
        memcpy(raw_arr,buffer_16,size_blob);
        free(buffer_16);
    } else if (raw_binary_depth == 32){
        /*32 bit data (put on Little Endian order)*/
        uint32_t *buffer_32=(uint32_t *)malloc(size_blob);
        memcpy(buffer_32,blob_buffer,size_blob);
        for (i = 0; i < n_elems_data; i++) {
            buffer_32[i]=((buffer_32[i]>>24) & 0x000000ff) | // move byte 3 to byte 0
                         ((buffer_32[i]<<8 ) & 0x00ff0000) | // move byte 1 to byte 2
                         ((buffer_32[i]>>8 ) & 0x0000ff00) | // move byte 2 to byte 1
                         ((buffer_32[i]<<24) & 0xff000000);  // move byte 0 to byte 3
        }
        memcpy(raw_arr,buffer_32,size_blob);
        free(buffer_32);
    }
    free(blob_buffer);

    if(L_DEBUG_OUTPUT_1) fprintf(stdout,"  n_elems_data = %ld\n",n_elems_data);

    //update
    rb5_param->size_blob   =size_blob;
    if(rb5_param->n_elems_data != n_elems_data){
        fprintf(stdout,"  INCONSISTENT rb5_param->n_elems_data = %ld\n",rb5_param->n_elems_data);
        fprintf(stdout,"  INCONSISTENT n_elems_data = %ld\n",n_elems_data);
        return(EXIT_FAILURE);
    }
    rb5_param->n_elems_data=n_elems_data;

    //reorder
    reorder_by_iray_0degN(&(*rb5_param),&raw_arr);

    if(L_DEBUG_OUTPUT_1) dump_strRB5_PARAM_INFO(*rb5_param);
    *return_raw_arr=raw_arr;

    return(n_elems_data);    
}

//#############################################################################

char *map_rb5_to_h5_param(char *sparam){

  static char return_string[MAX_STRING]="\0";

               if (strcmp(sparam, "dBuZ"  ) == 0 ){; strcpy(return_string,"TH");
        } else if (strcmp(sparam, "dBuZv" ) == 0 ){; strcpy(return_string,"TV");
        } else if (strcmp(sparam, "dBZ"   ) == 0 ){; strcpy(return_string,"DBZH");
//        } else if (strcmp(sparam, "dBZv"  ) == 0 ){; strcpy(return_string,"DBZV");
        } else if (strcmp(sparam, "V"     ) == 0 ){; strcpy(return_string,"VRADH");
        } else if (strcmp(sparam, "Vv"    ) == 0 ){; strcpy(return_string,"VRADV");
        } else if (strcmp(sparam, "W"     ) == 0 ){; strcpy(return_string,"WRADH");
        } else if (strcmp(sparam, "Wv"    ) == 0 ){; strcpy(return_string,"WRADV");
        } else if (strcmp(sparam, "SNR"   ) == 0 ){; strcpy(return_string,"SNRH");
//        } else if (strcmp(sparam, "SNRv"  ) == 0 ){; strcpy(return_string,"SNRV");
        } else if (strcmp(sparam, "SQI"   ) == 0 ){; strcpy(return_string,"SQIH");
//        } else if (strcmp(sparam, "SQIv"  ) == 0 ){; strcpy(return_string,"SQIV");
//        } else if (strcmp(sparam, "ZDR"   ) == 0 ){; strcpy(return_string,"ZDR");
//        } else if (strcmp(sparam, "RhoHV" ) == 0 ){; strcpy(return_string,"RHOHV");
//        } else if (strcmp(sparam, "uPhiDP") == 0 ){; strcpy(return_string,"uPHIDP");
//        } else if (strcmp(sparam, "PhiDP" ) == 0 ){; strcpy(return_string,"PHIDP");
//        } else if (strcmp(sparam, "KDP"   ) == 0 ){; strcpy(return_string,"KDP");
        } else {
            strcpy(return_string,sparam);
            int i;
            for(i=0;i<strlen(return_string);i++){
                return_string[i]=toupper(return_string[i]);
            }
        }
    return(return_string);

}

//#############################################################################

strURPDATA what_is_this_param_to_urp(char *sparam){
    strURPDATA urp;
    // see /apps/urp/build/include/drpdecode.h
                   urp.type=-1;
            strcpy(urp.name,"n/a");
            strcpy(urp.unit,"n/a");
            strcpy(urp.desc,"n/a");
               if (strcmp(sparam, "dataflag") == 0){
        } else if (strcmp(sparam, "startangle") == 0){
        } else if (strcmp(sparam, "stopangle") == 0){
        } else if (strcmp(sparam, "startfixangle") == 0){
        } else if (strcmp(sparam, "stopafixngle") == 0){
        } else if (strcmp(sparam, "numpulses") == 0 ){
        } else if (strcmp(sparam, "timestamp") == 0 ){
        } else if (strcmp(sparam, "txpower") == 0 ){
        } else if (strcmp(sparam, "noisepowerh") == 0 ){
        } else if (strcmp(sparam, "noisepowerv") == 0 ){
        } else if (strcmp(sparam, "dBuZ") == 0 ){
                   urp.type=1;
            strcpy(urp.name,"DBT");
            strcpy(urp.unit,"dBZ");
            strcpy(urp.desc,"Uncorrected Reflectivity");
        } else if (strcmp(sparam, "dBZ") == 0 ){
                   urp.type=2;
            strcpy(urp.name,"DBZ");
            strcpy(urp.unit,"dBZ");
            strcpy(urp.desc,"Reflectivity");
        } else if (strcmp(sparam, "V") == 0 ){
                   urp.type=3;
            strcpy(urp.name,"VEL");
            strcpy(urp.unit,"m/s");
            strcpy(urp.desc,"Velocity");
        } else if (strcmp(sparam, "W") == 0 ){
                   urp.type=4;
            strcpy(urp.name,"WID");
            strcpy(urp.unit,"m/s");
            strcpy(urp.desc,"Width");
        } else if (strcmp(sparam, "SNR") == 0 ){
                   urp.type=100;
            strcpy(urp.name,"SNR");
            strcpy(urp.unit,"");
            strcpy(urp.desc,"Signal to Noise");
        } else if (strcmp(sparam, "SQI") == 0 ){
                   urp.type=18;
            strcpy(urp.name,"SQI");
            strcpy(urp.unit,"");
            strcpy(urp.desc,"Signal Quality Index");
        } else if (strcmp(sparam, "ZDR") == 0 ){
                   urp.type=5;
            strcpy(urp.name,"ZDR");
            strcpy(urp.unit,"dB");
            strcpy(urp.desc,"Differential Reflectivity");
        } else if (strcmp(sparam, "RhoHV") == 0 ){
                   urp.type=19;
            strcpy(urp.name,"RHOHV");
            strcpy(urp.unit,"");
            strcpy(urp.desc,"Correlation Coefficient");
        } else if (strcmp(sparam, "uPhiDP") == 0 ){
                   urp.type=16;
            strcpy(urp.name,"UPHIDP");
            strcpy(urp.unit,"deg");
            strcpy(urp.desc,"Uncorrected Differential Phase");
        } else if (strcmp(sparam, "PhiDP") == 0 ){
                   urp.type=16;
            strcpy(urp.name,"PHIDP");
            strcpy(urp.unit,"deg");
            strcpy(urp.desc,"Differential Phase");
        } else if (strcmp(sparam, "uKDP") == 0 ){
                   urp.type=14;
            strcpy(urp.name,"UKDP");
            strcpy(urp.unit,"deg/km");
            strcpy(urp.desc,"Uncorrected Specific Differential Phase");
        } else if (strcmp(sparam, "KDP") == 0 ){
                   urp.type=14;
            strcpy(urp.name,"KDP");
            strcpy(urp.unit,"deg/km");
            strcpy(urp.desc,"Specific Differential Phase");
        }
    return(urp);
}

//#############################################################################

void close_rb5_info(strRB5_INFO *rb5_info){

  if(rb5_info->xpathCtx != NULL) xmlXPathFreeContext(rb5_info->xpathCtx); //cleanup
  if(rb5_info->doc      != NULL) xmlFreeDoc(rb5_info->doc); // free the document
  if(rb5_info->buffer   != NULL) free(rb5_info->buffer); // free entire file buffer

  int this_slice;  
  for (this_slice = 0; this_slice < rb5_info->n_slices; this_slice++){
    if(rb5_info->slice_moving_angle_start_arr[this_slice] != NULL) free(rb5_info->slice_moving_angle_start_arr[this_slice]);
    if(rb5_info->slice_moving_angle_stop_arr[this_slice] != NULL) free(rb5_info->slice_moving_angle_stop_arr[this_slice]);
    if(rb5_info->slice_fixed_angle_start_arr[this_slice] != NULL) free(rb5_info->slice_fixed_angle_start_arr[this_slice]);
    if(rb5_info->slice_fixed_angle_stop_arr[this_slice] != NULL) free(rb5_info->slice_fixed_angle_stop_arr[this_slice]);

    if(rb5_info->slice_moving_angle_arr[this_slice] != NULL) free(rb5_info->slice_moving_angle_arr[this_slice]);
    if(rb5_info->slice_fixed_angle_arr[this_slice] != NULL) free(rb5_info->slice_fixed_angle_arr[this_slice]);
  }
  xmlCleanupParser(); // free globals

}

//#############################################################################

float get_pw_by_index(strRB5_INFO *rb5_info, size_t this_pw_idx) {

    float pw_val_arr[MAX_PULSE_WIDTHS];

         if(strcmp(rb5_info->sensor_id,"XKR") == 0){ //CAX1 @ Albert Head, BC & King Radar, ON
    pw_val_arr[0]=0.3;
    pw_val_arr[1]=1.0;
    pw_val_arr[2]=2.0;
    pw_val_arr[3]=3.3;
  } else if(strcmp(rb5_info->sensor_id,"XWH") == 0){ //CAX1 @ Whitehorse, YK
    pw_val_arr[0]=0.3;
    pw_val_arr[1]=1.0;
    pw_val_arr[2]=2.0;
    pw_val_arr[3]=3.3;
  } else if(strcmp(rb5_info->sensor_id,"XSO") == 0){ //CAX2 @ Exeter, ON
    pw_val_arr[0]=0.3;
    pw_val_arr[1]=1.0;
    pw_val_arr[2]=2.0;
    pw_val_arr[3]=3.3;
  } else if(strcmp(rb5_info->sensor_id,"X##") == 0){ //CAX3 @ ???, ??
    pw_val_arr[0]=0.3;
    pw_val_arr[1]=1.0;
    pw_val_arr[2]=2.0;
    pw_val_arr[3]=3.3;
  }
  return(pw_val_arr[this_pw_idx]);
}

//#############################################################################

char *get_xpath_slice_attrib(const xmlXPathContextPtr xpathCtx, size_t this_slice, char *xpath_end) {

  char xpath[MAX_STRING]="\0";
  char xpath_bgn[MAX_STRING]="\0";
  static char return_string[MAX_STRING]="\0";
  int iSLICE=0;
  int ifoundSLICE=0;
  //compare this_SLICE vs iSLICE=0
  iSLICE=this_slice;
  sprintf(xpath_bgn,"(/volume/scan/slice)[%2d]",iSLICE+1);
  if(                        get_xpath_size(xpathCtx,strcat(strcpy(xpath,xpath_bgn),xpath_end))){
    strcpy(return_string,return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),xpath_end)));
    ifoundSLICE=iSLICE;
  } else {
    iSLICE=0;
    sprintf(xpath_bgn,"(/volume/scan/slice)[%2d]",iSLICE+1);
    strcpy(return_string,return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),xpath_end)));
    ifoundSLICE=iSLICE;
  }
if(L_DEBUG_OUTPUT_2) fprintf(stdout,"ifoundSLICE = %2d : %s = %s\n",ifoundSLICE,xpath_end,return_string);
  return(return_string); 
  
}

//#############################################################################

int populate_rb5_info(strRB5_INFO *rb5_info, int L_VERBOSE){

    // init pointers to NULL
    rb5_info->xpathCtx=NULL;
    rb5_info->doc=NULL;

    //get byte offset of non-XML <BLOB> dataspace, to used by return_xpath_blobid_raw()
    char substring[]="<!-- END XML -->";
    char *match=NULL;
    match=strstr(rb5_info->buffer,substring);
    rb5_info->byte_offset_blobspace=match-(rb5_info->buffer)+strlen(substring)+1; //count trailing \n
    if(L_DEBUG_OUTPUT_1) fprintf(stdout,"byte_offset_blobspace = %ld\n", rb5_info->byte_offset_blobspace);
    if (rb5_info->byte_offset_blobspace == 0) {
        fprintf(stderr,"Cannot read XML header in %s\n", rb5_info->inp_fullfile);
        close_rb5_info(&(*rb5_info));
        return(EXIT_FAILURE);
    }

    // init libxml
    xmlInitParser();
    LIBXML_TEST_VERSION;

    // parse the XML header and get the DOM
    xmlDoc *doc = xmlReadMemory(rb5_info->buffer, rb5_info->byte_offset_blobspace, "noname.xml", NULL, 0);
    if (doc == NULL) {
        fprintf(stderr,"Error while parsing XML header in file = %s\n", rb5_info->inp_fullfile);
        close_rb5_info(&(*rb5_info));
        return(EXIT_FAILURE);
    }
    rb5_info->doc=doc;

    // create xpath evaluation context
    xmlXPathContextPtr xpathCtx = xmlXPathNewContext(doc);
    if(xpathCtx == NULL) {
        fprintf(stderr,"Error: unable to create new XPath context\n");
        close_rb5_info(&(*rb5_info));
        return(EXIT_FAILURE);
    }
    rb5_info->xpathCtx=xpathCtx;

    char stmpa[MAX_STRING]="\0";
    strcpy(stmpa,rb5_info->inp_fullfile);
    strcpy(rb5_info->inp_file_basename,basename(stmpa));
    strcpy(stmpa,rb5_info->inp_fullfile);
    strcpy(rb5_info->inp_file_dirname , dirname(stmpa));
    static char inp_file_data_type[MAX_STRING]="\0";
    char *lastdot=strrchr(rb5_info->inp_file_basename,'.');
    int dot_position=lastdot-rb5_info->inp_file_basename;
    int ext_length=strlen(rb5_info->inp_file_basename)-dot_position;
    int typ_length=strlen(rb5_info->inp_file_basename)-16-ext_length;
    strncpy(inp_file_data_type,(rb5_info->inp_file_basename)+16,typ_length);
    strncpy(rb5_info->inp_file_data_type,inp_file_data_type,typ_length);
    rb5_info->inp_file_data_type[typ_length]='\0'; //add NULL terminator
    if(L_VERBOSE){
        fprintf(stdout,"%s = %s\n", "rb5_info->inp_file_dirname", rb5_info->inp_file_dirname);
        fprintf(stdout,"%s = %s\n", "rb5_info->inp_file_basename", rb5_info->inp_file_basename);
        fprintf(stdout,"%s = %s\n", "rb5_info->inp_file_data_type", rb5_info->inp_file_data_type);
    }

    strRB5_PARAM_INFO rb5_param;

    char xpath[MAX_STRING]="\0";
    char xpath_bgn[MAX_STRING]="\0";

    strcpy(rb5_info->rainbow_version,return_xpath_value(xpathCtx,"/volume/@version"));
    if(strcmp(rb5_info->rainbow_version,MINIMUM_RAINBOW_VERSION) < 0){
        fprintf(stderr,"Error: Incompatible Rainbow version, this is v%s, (v%s minumum)\n",rb5_info->rainbow_version,MINIMUM_RAINBOW_VERSION);
        close_rb5_info(&(*rb5_info));
        return(EXIT_FAILURE);
    }
    
    strcpy(rb5_info->xml_block_name,return_xpath_name(xpathCtx,"/*[1]")); //top level name query
    if(strcmp(rb5_info->xml_block_name,"volume") != 0){
        fprintf(stderr,"Error: This is not a Rainbow raw file, expecting <volume>, this is a <%s>\n",rb5_info->xml_block_name);
        close_rb5_info(&(*rb5_info));
        return(EXIT_FAILURE);
    }

    strcpy(rb5_info->xml_block_type   ,return_xpath_value(xpathCtx,"/volume/@type"));
    strcpy(rb5_info->xml_block_iso8601,return_xpath_value(xpathCtx,"/volume/@datetime"));
    strncpy(rb5_info->xml_block_iso8601+10," ",1); //blank T-delimiter
    if(L_VERBOSE){
        fprintf(stdout,"%s = %s\n", "rb5_info->rainbow_version"  , rb5_info->rainbow_version);
        fprintf(stdout,"%s = %s\n", "rb5_info->xml_block_name"   , rb5_info->xml_block_name);
        fprintf(stdout,"%s = %s\n", "rb5_info->xml_block_type"   , rb5_info->xml_block_type);
        fprintf(stdout,"%s = %s\n", "rb5_info->xml_block_iso8601", rb5_info->xml_block_iso8601);
    }

    strcpy(xpath_bgn,"/volume/sensorinfo");
    strcpy(rb5_info->sensor_id           ,     return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"/@id")));
    strcpy(rb5_info->sensor_name         ,     return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"/@name")));
           rb5_info->sensor_lon_deg      =atof(return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"/lon")));
           rb5_info->sensor_lat_deg      =atof(return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"/lat")));
           rb5_info->sensor_alt_m        =atof(return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"/alt")));
           rb5_info->sensor_wavelength_cm=atof(return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"/wavelen")))*100.;
           rb5_info->sensor_beamwidth_deg=atof(return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"/beamwidth")));
    if(L_VERBOSE){
        fprintf(stdout,"%s = %s\n"  , "rb5_info->sensor_id"           , rb5_info->sensor_id);
        fprintf(stdout,"%s = %s\n"  , "rb5_info->sensor_name"         , rb5_info->sensor_name);
        fprintf(stdout,"%s = %.5f\n", "rb5_info->sensor_lon_deg"      , rb5_info->sensor_lon_deg);
        fprintf(stdout,"%s = %.5f\n", "rb5_info->sensor_lat_deg"      , rb5_info->sensor_lat_deg);
        fprintf(stdout,"%s = %.1f\n", "rb5_info->sensor_alt_m"        , rb5_info->sensor_alt_m);
        fprintf(stdout,"%s = %.3f\n", "rb5_info->sensor_wavelength_cm", rb5_info->sensor_wavelength_cm);
        fprintf(stdout,"%s = %.2f\n", "rb5_info->sensor_beamwidth_deg", rb5_info->sensor_beamwidth_deg);
    }

    rb5_info->history_exists=0;
    strcpy(xpath_bgn,"/volume/history"); //check for this named block
    if(strcmp(return_xpath_name(xpathCtx,xpath_bgn),"history") == 0){
        rb5_info->history_exists=1;
        strcpy(rb5_info->history_pdfname   ,return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"/@pdfname")));
        strcpy(rb5_info->history_ppdfname  ,return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"/@ppdfname")));
        strcpy(rb5_info->history_sdfname   ,return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"/@sdfname")));
        if(L_VERBOSE){
            fprintf(stdout,"%s = %s\n", "rb5_info->history_pdfname" , rb5_info->history_pdfname);
            fprintf(stdout,"%s = %s\n", "rb5_info->history_ppdfname", rb5_info->history_ppdfname);
            fprintf(stdout,"%s = %s\n", "rb5_info->history_sdfname" , rb5_info->history_sdfname);
        }        
        strcpy(xpath_bgn,"/volume/history/rawdatafiles/file");
        rb5_info->history_n_rawdatafiles=get_xpath_size(xpathCtx,xpath_bgn);
        if(L_VERBOSE){
            fprintf(stdout,"%s = %ld\n", "rb5_info->history_n_rawdatafiles" , rb5_info->history_n_rawdatafiles);
        }
        size_t this_rawdatafile;
        for (this_rawdatafile = 0; this_rawdatafile < rb5_info->history_n_rawdatafiles; this_rawdatafile++){
            sprintf(xpath,"(%s)[%2ld]",xpath_bgn,this_rawdatafile+1);
            strcpy(rb5_info->history_rawdatafiles_arr[this_rawdatafile],return_xpath_value(xpathCtx,xpath));
            if(L_VERBOSE){
                fprintf(stdout,"%s = %s\n", xpath, rb5_info->history_rawdatafiles_arr[this_rawdatafile]);
            }
        }
        strcpy(xpath_bgn,"/volume/history/preprocessedfiles/file");
        rb5_info->history_n_preprocessedfiles=get_xpath_size(xpathCtx,xpath_bgn);
        if(L_VERBOSE){
            fprintf(stdout,"%s = %ld\n", "rb5_info->history_n_preprocessedfiles" , rb5_info->history_n_preprocessedfiles);
        }
        size_t this_preprocessedfile;
        for (this_preprocessedfile = 0; this_preprocessedfile < rb5_info->history_n_preprocessedfiles; this_preprocessedfile++){
            sprintf(xpath,"(%s)[%2ld]",xpath_bgn,this_preprocessedfile+1);
            strcpy(rb5_info->history_preprocessedfiles_arr[this_preprocessedfile],return_xpath_value(xpathCtx,xpath));
            if(L_VERBOSE){
                fprintf(stdout,"%s = %s\n", xpath, rb5_info->history_preprocessedfiles_arr[this_preprocessedfile]);
            }
        }
    }

    strcpy(rb5_info->scan_type,rb5_info->xml_block_type); //copy from xml_block
    strcpy(stmpa,return_xpath_value(xpathCtx,"/volume/scan/@name"));
    strncpy(rb5_info->scan_name,stmpa,strlen(stmpa)-strlen(rb5_info->scan_type)-1);
    rb5_info->scan_name[strlen(stmpa)-strlen(rb5_info->scan_type)-1]='\0'; // place the null terminator
    if(L_VERBOSE){
        fprintf(stdout,"%s = %s\n", "rb5_info->scan_name"           , rb5_info->scan_name);
        fprintf(stdout,"%s = %s\n", "rb5_info->scan_type"           , rb5_info->scan_type);
    }

    int this_slice=0;
    size_t this_rayinfo;
    size_t this_rawdata;
    int L_RB5_PARAM_VERBOSE=L_VERBOSE;

    //init
    rb5_info->iray_0degN[this_slice]=-1;

    //RAYINFO
    sprintf(xpath_bgn,"((/volume/scan/slice)[%2d]/slicedata/%s)",this_slice+1,"rayinfo");
    rb5_info->n_rayinfos=get_xpath_size(xpathCtx,xpath_bgn);
    for (this_rayinfo = 0; this_rayinfo < rb5_info->n_rayinfos; this_rayinfo++){
      sprintf(xpath_bgn,"((/volume/scan/slice)[%2d]/slicedata/%s)[%2ld]/",this_slice+1,"rayinfo",this_rayinfo+1);
      rb5_param=get_rb5_param_info(rb5_info,xpath_bgn,L_RB5_PARAM_VERBOSE);
      strcpy(rb5_info->rayinfo_name_arr[this_rayinfo],rb5_param.sparam);
    } //for (this_rayinfo = 0; this_rayinfo < rb5_info->n_rayinfos; this_rayinfo++){

    if(L_DEBUG_OUTPUT_1){
      fprintf(stdout,"  rb5_info->n_rayinfos = %ld\n",rb5_info->n_rayinfos);
      fprintf(stdout,"  rb5_info->rayinfo_name_arr = [");
      for (this_rayinfo = 0; this_rayinfo < rb5_info->n_rayinfos; this_rayinfo++){
        if (this_rayinfo == 0) {
          fprintf(stdout,  "%s",rb5_info->rayinfo_name_arr[this_rayinfo]);
        } else {
          fprintf(stdout,", %s",rb5_info->rayinfo_name_arr[this_rayinfo]);
        }
      } //for (this_rayinfo = 0; this_rayinfo < rb5_info->n_rayinfos; this_rayinfo++){
      fprintf(stdout,"]\n");
    }

    //RAWDATA
    sprintf(xpath_bgn,"((/volume/scan/slice)[%2d]/slicedata/%s)",this_slice+1,"rawdata");
    rb5_info->n_rawdatas=get_xpath_size(xpathCtx,xpath_bgn);
    for (this_rawdata = 0; this_rawdata < rb5_info->n_rawdatas; this_rawdata++){
      sprintf(xpath_bgn,"((/volume/scan/slice)[%2d]/slicedata/%s)[%2ld]/",this_slice+1,"rawdata",this_rawdata+1);
      rb5_param=get_rb5_param_info(rb5_info,xpath_bgn,L_RB5_PARAM_VERBOSE);
      strcpy(rb5_info->rawdata_name_arr[this_rawdata],rb5_param.sparam);
    } //for (this_rawdata = 0; this_rawdata < rb5_info->n_rawdatas; this_rawdata++){

    if(L_DEBUG_OUTPUT_1) {
      fprintf(stdout,"  rb5_info->n_rawdatas = %ld\n",rb5_info->n_rawdatas);
      fprintf(stdout,"  rb5_info->rawdata_name_arr = [");
      for (this_rawdata = 0; this_rawdata < rb5_info->n_rawdatas; this_rawdata++){
        if (this_rawdata == 0) {
          fprintf(stdout,  "%s",rb5_info->rawdata_name_arr[this_rawdata]);
        } else {
          fprintf(stdout,", %s",rb5_info->rawdata_name_arr[this_rawdata]);
        }
      } //for (this_rawdata = 0; this_rawdata < rb5_info->n_rawdatas; this_rawdata++){
      fprintf(stdout,"]\n");
    } //if(L_DEBUG_OUTPUT_1) {

    strcpy(xpath,"/volume/scan/pargroup/numele");
    rb5_info->n_slices=atoi(return_xpath_value(xpathCtx,xpath));
    if(L_VERBOSE){
        fprintf(stdout,"%s = %ld\n", "rb5_info->n_slices", rb5_info->n_slices);
    }

    char req_rawdata_name[MAX_STRING]="\0";
    int idx_req=-1;
    for (this_slice = 0; this_slice < rb5_info->n_slices; this_slice++){

        //init
        rb5_info->iray_0degN[this_slice]=-1;

        //update dims
            strcpy(req_rawdata_name,"dBZ"); //mandatory
       if(strcmp(rb5_info->inp_file_data_type, "ALL") == 0){
           strcpy(req_rawdata_name,"dBZ"); //mandatory
       } else {
           strcpy(req_rawdata_name,rb5_info->inp_file_data_type); //self
       }
        idx_req=find_in_string_arr(rb5_info->rawdata_name_arr,rb5_info->n_rawdatas,req_rawdata_name);
        if(idx_req == -1) {
            fprintf(stdout,"IMPOSSIBLE: %s not found\n", req_rawdata_name);
        } else {
            sprintf(xpath_bgn,"((/volume/scan/slice)[%2d]/slicedata/%s)[%2d]/",this_slice+1,"rawdata",idx_req+1);
            rb5_param=get_rb5_param_info(rb5_info,xpath_bgn,L_RB5_PARAM_VERBOSE);
            rb5_info->nrays[this_slice]=rb5_param.nrays;
            rb5_info->nbins[this_slice]=rb5_param.nbins;
            rb5_info->n_elems_data[this_slice]=rb5_param.n_elems_data;
        }

        sprintf(xpath_bgn,"(/volume/scan/slice)[%2d]",this_slice+1);
        // Note: using get_xpath_slice_attrib() to cycle thru 0th slice upward
        strcpy(rb5_info->slice_iso8601_bgn      [this_slice],get_xpath_iso8601_attrib(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"/slicedata/")));
        get_slice_end_iso8601(&(*rb5_info),      this_slice);
               rb5_info->angle_deg_arr          [this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/posangle"));
               rb5_info->slice_nyquist_vel      [this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/dynv/@max"));
               rb5_info->slice_nyquist_wid      [this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/dynw/@max"));
               rb5_info->slice_bin_range_res_km [this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/rangestep"));
               rb5_info->slice_bin_range_bgn_km [this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/start_range"));
               rb5_info->slice_bin_range_end_km [this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/stoprange"));
               rb5_info->slice_ray_angle_res_deg[this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/anglestep"));
               rb5_info->slice_ray_angle_bgn_deg[this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/startangle"));
               rb5_info->slice_ray_angle_end_deg[this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/stopangle"));
               rb5_info->slice_pw_index         [this_slice]= atoi(get_xpath_slice_attrib(xpathCtx,this_slice,"/pw_index"));
               rb5_info->slice_pw_microsec      [this_slice]=get_pw_by_index(rb5_info,rb5_info->slice_pw_index[this_slice]);
               rb5_info->slice_antspeed_deg_sec [this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/antspeed"));
               rb5_info->slice_antspeed_rpm     [this_slice]= rb5_info->slice_antspeed_deg_sec [this_slice]/360.*60.;
               rb5_info->slice_num_samples      [this_slice]= atoi(get_xpath_slice_attrib(xpathCtx,this_slice,"/timesamp"));
        strcpy(rb5_info->slice_dual_prf_mode    [this_slice],      get_xpath_slice_attrib(xpathCtx,this_slice,"/dualprfmode"));
        strcpy(rb5_info->slice_prf_stagger      [this_slice],      get_xpath_slice_attrib(xpathCtx,this_slice,"/stagger"));
               rb5_info->slice_hi_prf           [this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/highprf"));
               rb5_info->slice_lo_prf           [this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/lowprf"));
               rb5_info->slice_csr_threshold    [this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/csr"));
               rb5_info->slice_sqi_threshold    [this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/sqi"));
               rb5_info->slice_zsqi_threshold   [this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/zsqi"));
               rb5_info->slice_log_threshold    [this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/log"));
               rb5_info->slice_noise_power_h    [this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/noise_power_dbz"));
               rb5_info->slice_noise_power_v    [this_slice]= atof(get_xpath_slice_attrib(xpathCtx,this_slice,"/noise_power_dbz_dpv"));

        static char rspdphradconst[MAX_STRING]="\0";
        static char rspdpvradconst[MAX_STRING]="\0";
        strcpy(rspdphradconst,get_xpath_slice_attrib(xpathCtx,this_slice,"/rspdphradconst"));
        strcpy(rspdpvradconst,get_xpath_slice_attrib(xpathCtx,this_slice,"/rspdpvradconst"));
        //get <pw_index>'th field
        // code ref: http://stackoverflow.com/questions/11198604/c-split-string-into-an-array-of-strings
        char *pw_array[MAX_PULSE_WIDTHS+1];
        char delimiters[]=" ,\t\n";
        char *token;
        int i;
        i=-1;
        token=strtok(rspdphradconst,delimiters);
        while(token != NULL){
          pw_array[++i]=token;
          token=strtok(NULL,delimiters);
        }
        rb5_info->slice_radconst_h[this_slice]=atof(pw_array[rb5_info->slice_pw_index[this_slice]]);
        i=-1;
        token=strtok(rspdpvradconst,delimiters);
        while(token != NULL){
          pw_array[++i]=token;
          token=strtok(NULL,delimiters);
        }
        rb5_info->slice_radconst_v[this_slice]=atof(pw_array[rb5_info->slice_pw_index[this_slice]]);

        if(L_VERBOSE){
            strncpy(stmpa,rb5_info->slice_dual_prf_mode[this_slice]+strlen("SdfDPrfMode"),3);
            stmpa[3]='\0'; //add NULL terminator
            fprintf(stdout," %s -> %s @ %05.2f deg, %5.3f km_res, %4.2f deg_res, %4ld samples, PRF(%3s)=%4.0f/%4.0f (%s to %s, %.3f sec)\n",
                xpath_bgn,
                     return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"/slicedata/rawdata/@type")),
                rb5_info->angle_deg_arr[this_slice],
                rb5_info->slice_bin_range_res_km[this_slice],
                rb5_info->slice_ray_angle_res_deg[this_slice],
                rb5_info->slice_num_samples[this_slice],
                stmpa,
                rb5_info->slice_hi_prf[this_slice],
                rb5_info->slice_lo_prf[this_slice],
                rb5_info->slice_iso8601_bgn[this_slice],
                rb5_info->slice_iso8601_end[this_slice],
                rb5_info->slice_dur_secs[this_slice]
            );
        }

        //needed angle_deg_arr & slice_ray_angle_res_deg
        //calculate moving and fixed average ray readbacks
        get_slice_mid_angle_readbacks(&(*rb5_info),this_slice);
        //get iray_0degN, updates rb5_info->slice_moving_angle_arr
        get_slice_iray_0degN(&(*rb5_info),this_slice);

    } //for (this_slice = 0; this_slice < rb5_info->n_slices; this_slice++){
    if(L_VERBOSE){
        fprintf(stdout,"\n");
    }

    return(EXIT_SUCCESS);

}

//#############################################################################

strRB5_PARAM_INFO get_rb5_param_info(strRB5_INFO *rb5_info, char *xpath_bgn, int L_VERBOSE) {

    const xmlXPathContextPtr xpathCtx=rb5_info->xpathCtx;
    strRB5_PARAM_INFO rb5_param;
    strcpy(rb5_param.xpath_bgn,xpath_bgn);
    char xpath[MAX_STRING]="\0";

    //get this_slice and update rb5_param.iray_0degN
    int this_slice;
    //sprintf(xpath_bgn,"((/volume/scan/slice)[%2d]/slicedata/%s)[%2d]/",this_slice+1,"rawdata",idx_req+1);
    sscanf(xpath_bgn,"%*[(]/volume/scan/slice)[%2d]%*[.]",&this_slice); //skip leading slashes and trailing chars
    this_slice-=1; //decrement from string
    rb5_param.iray_0degN=rb5_info->iray_0degN[this_slice];

    //iso8601 is in the parent <slicedata>
    strcpy(rb5_param.iso8601,get_xpath_iso8601_attrib(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"../")));

    if(strstr(xpath_bgn,"rawdata") != NULL) {
      //  ./get_xpath_val 2016090715102400dBZ.vol "((/volume/scan/slice)[1]/slicedata/rawdata)[1]/@type"
      //  XPATH: ((/volume/scan/slice)[1]/slicedata/rawdata)[1]/@type = dBZ
      strcpy(rb5_param.sparam,               return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@type"  )));
             rb5_param.blobid          =atoi(return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@blobid")));
             rb5_param.raw_binary_depth=atoi(return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@depth" )));
             rb5_param.nrays           =atoi(return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@rays"  )));
             rb5_param.nbins           =atoi(return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@bins"  )));
             rb5_param.data_range_min  =atof(return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@min"   )));
             rb5_param.data_range_max  =atof(return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@max"   )));
      rb5_param.data_bytesize=rb5_param.raw_binary_depth/8;
      if(L_VERBOSE){
        fprintf(stdout,"%s%s = %s [%ld,%ld] (%ld-byte) range={%.4f,%.4f} (%s)\n",
          xpath_bgn,
          "@type",
          rb5_param.sparam,
          rb5_param.nrays,
          rb5_param.nbins,
          rb5_param.data_bytesize,
          rb5_param.data_range_min,
          rb5_param.data_range_max,
          rb5_param.iso8601
        );
      } //if(L_VERBOSE) {
    } else {
      //  ./get_xpath_val 2016090715102400dBZ.vol "((/volume/scan/slice)[1]/slicedata/rayinfo)[6]/@refid"
      //  XPATH: ((/volume/scan/slice)[1]/slicedata/rayinfo)[6]/@refid = numpulses
      strcpy(rb5_param.sparam,               return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@refid"  )));
             rb5_param.blobid          =atoi(return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@blobid")));
             rb5_param.raw_binary_depth=atoi(return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@depth" )));
             rb5_param.nrays           =atoi(return_xpath_value(xpathCtx,strcat(strcpy(xpath,xpath_bgn),"@rays"  )));
             rb5_param.nbins=1;
             rb5_param.data_range_min=-999;
             rb5_param.data_range_max=-999;
      rb5_param.data_bytesize=rb5_param.raw_binary_depth/8;
      if(L_VERBOSE){
        fprintf(stdout,"%s%s = %s [%ld] (%ld-byte) (%s)\n",
          xpath_bgn,
          "@refid",
          rb5_param.sparam,
          rb5_param.nrays,
          rb5_param.data_bytesize,
          rb5_param.iso8601
        );
      } //if(L_VERBOSE) {
    }

    // pre-calc, to be confirmed by return_param_blobid_raw()
    rb5_param.n_elems_data=rb5_param.nrays*rb5_param.nbins;

    rb5_param.raw_binary_min=(1L<<          0               );
    rb5_param.raw_binary_max=(1L<<rb5_param.raw_binary_depth)-1;
    rb5_param.raw_binary_width=rb5_param.raw_binary_max-rb5_param.raw_binary_min;
    rb5_param.data_range_width=rb5_param.data_range_max-rb5_param.data_range_min;
    rb5_param.data_step=rb5_param.data_range_width/rb5_param.raw_binary_width;

            if((strcmp(rb5_param.sparam, "dataflag") == 0) ||
               (strcmp(rb5_param.sparam, "numpulses") == 0 ) ||
               (strcmp(rb5_param.sparam, "timestamp") == 0 ) ||
               (strcmp(rb5_param.sparam, "txpower") == 0 ) ||
               (strcmp(rb5_param.sparam, "noisepowerh") == 0 ) ||
               (strcmp(rb5_param.sparam, "noisepowerv") == 0 )) {
        strcpy(rb5_param.conversion,"copy");
    } else  if((strcmp(rb5_param.sparam, "startangle") == 0) ||
               (strcmp(rb5_param.sparam, "stopangle") == 0 ) ||
               (strcmp(rb5_param.sparam, "startfixangle") == 0 ) ||
               (strcmp(rb5_param.sparam, "stopfixangle") == 0 )) {
        strcpy(rb5_param.conversion,"angular");
    } else  if( (strcmp(rb5_param.sparam, "uPhiDP") == 0) ||
               (strcmp(rb5_param.sparam, "PhiDP") == 0 )) {
        strcpy(rb5_param.conversion,"phidp_data");
    } else  if( (strcmp(rb5_param.sparam, "uKDP") == 0) ||
               (strcmp(rb5_param.sparam, "KDP") == 0 )) {
        strcpy(rb5_param.conversion,"kdp_data");
    } else {
        strcpy(rb5_param.conversion,"reg_data");
    }

    rb5_param.NODATA_val=-999; //TBD

    return(rb5_param);
}

//#############################################################################

size_t find_in_string_arr(char arr[][MAX_NSTRINGS], size_t n, char *match){
    int idx_req=-1;
    int i;
    for (i = 0; i < n; i++){
      if(strcmp(arr[i],match) == 0) {
        idx_req=i;
      }
    }
    return(idx_req);
};

//#############################################################################

void dump_strRB5_PARAM_INFO(strRB5_PARAM_INFO rb5_param){

    fprintf(stdout,"### dump of _strRB5_PARAM_INFO\n");
    fprintf(stdout,"--- xpath_bgn = %s\n",rb5_param.xpath_bgn);
    fprintf(stdout,"--- sparam = %s\n",rb5_param.sparam);
    fprintf(stdout,"--- blobid = %ld\n",rb5_param.blobid);
    fprintf(stdout,"--- size_blob = %ld\n",rb5_param.size_blob);
    fprintf(stdout,"--- n_elems_data = %ld\n",rb5_param.n_elems_data);
    fprintf(stdout,"--- data_bytesize = %ld\n",rb5_param.data_bytesize);

    fprintf(stdout,"--- raw_binary_depth = %ld\n",rb5_param.raw_binary_depth);
    fprintf(stdout,"--- raw_binary_min = %ld\n",rb5_param.raw_binary_min);
    fprintf(stdout,"--- raw_binary_max = %ld\n",rb5_param.raw_binary_max);
    fprintf(stdout,"--- raw_binary_width = %ld\n",rb5_param.raw_binary_width);

    fprintf(stdout,"--- nrays = %ld\n",rb5_param.nrays);
    fprintf(stdout,"--- nbins = %ld\n",rb5_param.nbins);
    fprintf(stdout,"--- iray_0degN = %ld\n",rb5_param.iray_0degN);

    fprintf(stdout,"--- conversion = %s\n",rb5_param.conversion);
    fprintf(stdout,"--- data_range_min = %f\n",rb5_param.data_range_min);
    fprintf(stdout,"--- data_range_max = %f\n",rb5_param.data_range_max);
    fprintf(stdout,"--- data_range_width = %f\n",rb5_param.data_range_width);
    fprintf(stdout,"--- data_step = %f\n",rb5_param.data_step);
    fprintf(stdout,"--- NODATA_val = %f\n",rb5_param.NODATA_val);

}

//#############################################################################

void get_slice_iray_0degN(strRB5_INFO *rb5_info, int req_slice){

    // variable used to re-order radial data such that angular readback is always increasing from 0degNorth
    int iray_0degN=-1; //flag for no reordering
    int i;
    size_t this_nrays=rb5_info->nrays[req_slice];
    if (strcmp(rb5_info->scan_type,"ele") == 0) {
        if(L_DEBUG_OUTPUT_1) fprintf(stdout,"  0degNorth reordering NOT APPLICABLE to RHI (ele) scans.\n");
    } else {
        // determine minimum ray startangle, should very close to 0degNorth,
        // for re-ordering radial data output
        iray_0degN=0; //init
        for (i = 0; i < this_nrays; i++) {
            if((rb5_info->slice_moving_angle_arr[req_slice])[i] < 
               (rb5_info->slice_moving_angle_arr[req_slice])[iray_0degN]) {
                iray_0degN=i;
          }
        }
    }
    rb5_info->iray_0degN[req_slice]=iray_0degN; //update

    //update slice_moving_angle_arr & its start & stop
    if(iray_0degN != -1){
        size_t p=iray_0degN; // handles 1-d data
        size_t n=this_nrays;
        float *org_arr=NULL;
        float *mod_arr=NULL;
        
        org_arr=((float *)rb5_info->slice_moving_angle_start_arr[req_slice]);
        mod_arr=(float *)malloc(n*sizeof(float));
        for (i=p;i<n;i++) mod_arr[i-   p ]=org_arr[i]; //output rays post 0-deg N
        for (i=0;i<p;i++) mod_arr[i+(n-p)]=org_arr[i]; //output rays pre  0-deg N
        rb5_info->slice_moving_angle_start_arr[req_slice]=mod_arr; //update
        free(org_arr);

        org_arr=((float *)rb5_info->slice_moving_angle_stop_arr[req_slice]);
        mod_arr=(float *)malloc(n*sizeof(float));
        for (i=p;i<n;i++) mod_arr[i-   p ]=org_arr[i]; //output rays post 0-deg N
        for (i=0;i<p;i++) mod_arr[i+(n-p)]=org_arr[i]; //output rays pre  0-deg N
        rb5_info->slice_moving_angle_stop_arr[req_slice]=mod_arr; //update
        free(org_arr);

        org_arr=((float *)rb5_info->slice_moving_angle_arr[req_slice]);
        mod_arr=(float *)malloc(n*sizeof(float));
        for (i=p;i<n;i++) mod_arr[i-   p ]=org_arr[i]; //output rays post 0-deg N
        for (i=0;i<p;i++) mod_arr[i+(n-p)]=org_arr[i]; //output rays pre  0-deg N
        rb5_info->slice_moving_angle_arr[req_slice]=mod_arr; //update
        free(org_arr);
   } 
}

//#############################################################################

void reorder_by_iray_0degN(strRB5_PARAM_INFO *rb5_param, void **input_raw_arr){

    size_t i;
    size_t this_n_elems_data=rb5_param->n_elems_data;
    size_t p=(rb5_param->iray_0degN)*(rb5_param->nbins); //handles 2-D data
    size_t n=this_n_elems_data;

    size_t raw_binary_depth=rb5_param->raw_binary_depth;

    if(rb5_param->iray_0degN != -1){
  
      //to be reordered
      void *deref_input_raw_arr=*input_raw_arr;
             if (raw_binary_depth ==  8){
        uint8_t  *buffer_8 =((uint8_t  *)deref_input_raw_arr);
        uint8_t  *output_raw_arr=(uint8_t  *)malloc(n*sizeof(uint8_t ));
        for (i=p;i<n;i++) output_raw_arr[i-   p ]=buffer_8 [i]; //output rays post 0-deg N
        for (i=0;i<p;i++) output_raw_arr[i+(n-p)]=buffer_8 [i]; //output rays pre  0-deg N
        free(buffer_8);
        *input_raw_arr=&(*output_raw_arr); //update
      } else if (raw_binary_depth == 16){
        uint16_t *buffer_16=((uint16_t *)deref_input_raw_arr);
        uint16_t *output_raw_arr=(uint16_t *)malloc(n*sizeof(uint16_t));
        for (i=p;i<n;i++) output_raw_arr[i-   p ]=buffer_16[i]; //output rays post 0-deg N
        for (i=0;i<p;i++) output_raw_arr[i+(n-p)]=buffer_16[i]; //output rays pre  0-deg N
        free(buffer_16);
        *input_raw_arr=&(*output_raw_arr); //update
      } else if (raw_binary_depth == 32){
        uint32_t *buffer_32=((uint32_t *)deref_input_raw_arr);
        uint32_t *output_raw_arr=(uint32_t *)malloc(n*sizeof(uint32_t));
        for (i=p;i<n;i++) output_raw_arr[i-   p ]=buffer_32[i]; //output rays post 0-deg N
        for (i=0;i<p;i++) output_raw_arr[i+(n-p)]=buffer_32[i]; //output rays pre  0-deg N
        free(buffer_32);
        *input_raw_arr=&(*output_raw_arr); //update
      }

    } //if(rb5_param->iray_0degN != -1){
}

//#############################################################################

void get_slice_end_iso8601(strRB5_INFO *rb5_info, int req_slice) {

    static char iso8601_bgn[MAX_STRING]="\0";
    strcpy(iso8601_bgn,rb5_info->slice_iso8601_bgn[req_slice]);
    static char iso8601_end[MAX_STRING]="\0";

    char xpath_bgn[MAX_STRING]="\0";

    // how many seconds did the slice take to complete
    float n_elapsed_secs;

    //if rayinfo <timestamp> exists, then extract and find largest elapsed n_secs
    //else estimate from antenna rotation
    char req_rayinfo_name[MAX_STRING]="\0";
    strcpy(req_rayinfo_name,"timestamp");
    int idx_req=find_in_string_arr(rb5_info->rayinfo_name_arr,rb5_info->n_rayinfos,req_rayinfo_name);
    if(idx_req != -1) {

      int L_RB5_PARAM_VERBOSE=0;
      sprintf(xpath_bgn,"((/volume/scan/slice)[%2d]/slicedata/%s)[%2d]/",req_slice+1,"rayinfo",idx_req+1);
      strRB5_PARAM_INFO rb5_param=get_rb5_param_info(rb5_info,xpath_bgn,L_RB5_PARAM_VERBOSE);

      float *data_arr=NULL;
      void *raw_arr=NULL;
      return_param_blobid_raw(&(*rb5_info), &rb5_param, &raw_arr);
      convert_raw_to_data(&rb5_param,&raw_arr,&data_arr);
    
      // determine maximum value in array
      size_t this_nrays=rb5_param.nrays;
      int iray_max_val=0;
      int i;
      for (i = 0; i < this_nrays; i++) {
        if(data_arr[i] > data_arr[iray_max_val]) {
          iray_max_val=i;
        }
      } //for (i = 0; i < this_nrays; i++) {
      n_elapsed_secs=data_arr[iray_max_val]/1000.;

      if ( raw_arr != NULL ) free( raw_arr);
      if (data_arr != NULL ) free(data_arr);

    } else { //if(idx_req == -1) {
      if(L_DEBUG_OUTPUT_1) fprintf(stdout,"  n_elapsed_secs ESTIMATED from <antspeed>\n");
      //antenna speed from <pargroup>
      float antspeed_deg_per_sec=atof(return_xpath_value(rb5_info->xpathCtx,"/volume/scan/pargroup/antspeed"));
      n_elapsed_secs=360./antspeed_deg_per_sec;
    } //else

    if(L_DEBUG_OUTPUT_1) fprintf(stdout,"  n_elapsed_secs = %f\n",n_elapsed_secs);

    rb5_info->slice_dur_secs[req_slice]=n_elapsed_secs;
    strcpy(iso8601_end,func_add_nsecs_2_iso8601(iso8601_bgn,n_elapsed_secs));
    strcpy(rb5_info->slice_iso8601_end[req_slice],iso8601_end);
    if(L_DEBUG_OUTPUT_1) fprintf(stdout,"  iso8601_bgn = %s\n",iso8601_bgn);
    if(L_DEBUG_OUTPUT_1) fprintf(stdout,"  iso8601_end = %s\n",iso8601_end);
    
}

//#############################################################################

void get_slice_mid_angle_readbacks(strRB5_INFO *rb5_info, int req_slice) {

    char req_rayinfo_name[MAX_STRING]="\0";
    int idx_req=-1;
    int L_RB5_PARAM_VERBOSE=0;
    char xpath_bgn[MAX_STRING]="\0";
    void *raw_arr=NULL;
    float *data_arr=NULL;

    size_t this_nrays=rb5_info->nrays[req_slice];
    int i;

    //limit readback to 0.001 precision
    float precision_factor=1000.;
    float default_val=-999.;

    rb5_info->slice_moving_angle_start_arr[req_slice]=malloc(this_nrays*sizeof(float));
    rb5_info->slice_moving_angle_stop_arr[req_slice]=malloc(this_nrays*sizeof(float));
    rb5_info->slice_fixed_angle_start_arr[req_slice]=malloc(this_nrays*sizeof(float));
    rb5_info->slice_fixed_angle_stop_arr[req_slice]=malloc(this_nrays*sizeof(float));

    rb5_info->slice_moving_angle_arr[req_slice]=malloc(this_nrays*sizeof(float));
    rb5_info->slice_fixed_angle_arr[req_slice]=malloc(this_nrays*sizeof(float));

    //moving_start_deg_arr
    strcpy(req_rayinfo_name,"startangle"); //mandatory
    idx_req=find_in_string_arr(rb5_info->rayinfo_name_arr,rb5_info->n_rayinfos,req_rayinfo_name);
    if(idx_req == -1) {
        fprintf(stdout,"IMPOSSIBLE: %s not found\n", req_rayinfo_name);
    } else {
        sprintf(xpath_bgn,"((/volume/scan/slice)[%2d]/slicedata/%s)[%2d]/",req_slice+1,"rayinfo",idx_req+1);
        strRB5_PARAM_INFO rb5_param=get_rb5_param_info(rb5_info,xpath_bgn,L_RB5_PARAM_VERBOSE);
        return_param_blobid_raw(&(*rb5_info), &rb5_param, &raw_arr);
        convert_raw_to_data(&rb5_param,&raw_arr,&data_arr);
        for (i = 0; i < this_nrays; i++) {
            //handle RHI -'ve elevation angles
            if(strcmp(rb5_info->scan_type,"ele") == 0){
                if(data_arr[i] > 270.){
                    data_arr[i]-=360.0;
                }
            }
            (rb5_info->slice_moving_angle_start_arr[req_slice])[i]=data_arr[i];
            (rb5_info->slice_moving_angle_start_arr[req_slice])[i]=roundf((rb5_info->slice_moving_angle_start_arr[req_slice])[i]*precision_factor)/precision_factor;
        } //for (i = 0; i < rb5_param.nrays; i++) {
        if ( raw_arr != NULL ) free( raw_arr);
        if (data_arr != NULL ) free(data_arr);
    }

    //moving_stop_deg_arr
    strcpy(req_rayinfo_name,"stopangle");
    idx_req=find_in_string_arr(rb5_info->rayinfo_name_arr,rb5_info->n_rayinfos,req_rayinfo_name);
    if(idx_req == -1) {
        for (i = 0; i < this_nrays; i++) {
            (rb5_info->slice_moving_angle_stop_arr[req_slice])[i]=(rb5_info->slice_moving_angle_start_arr[req_slice])[i]+rb5_info->slice_ray_angle_res_deg[req_slice];
            (rb5_info->slice_moving_angle_stop_arr[req_slice])[i]=roundf((rb5_info->slice_moving_angle_stop_arr[req_slice])[i]*precision_factor)/precision_factor;
        }
    } else {
        sprintf(xpath_bgn,"((/volume/scan/slice)[%2d]/slicedata/%s)[%2d]/",req_slice+1,"rayinfo",idx_req+1);
        strRB5_PARAM_INFO rb5_param=get_rb5_param_info(rb5_info,xpath_bgn,L_RB5_PARAM_VERBOSE);
        return_param_blobid_raw(&(*rb5_info), &rb5_param, &raw_arr);
        convert_raw_to_data(&rb5_param,&raw_arr,&data_arr);
        for (i = 0; i < this_nrays; i++) {
            //handle RHI -'ve elevation angles
            if(strcmp(rb5_info->scan_type,"ele") == 0){
                if(data_arr[i] > 270.){
                    data_arr[i]-=360.0;
                }
            }
            (rb5_info->slice_moving_angle_stop_arr[req_slice])[i]=data_arr[i];
            (rb5_info->slice_moving_angle_stop_arr[req_slice])[i]=roundf((rb5_info->slice_moving_angle_stop_arr[req_slice])[i]*precision_factor)/precision_factor;
        } //for (i = 0; i < rb5_param.nrays; i++) {
        if ( raw_arr != NULL ) free( raw_arr);
        if (data_arr != NULL ) free(data_arr);
    }
    
    //fixed_start_deg_arr
    default_val=rb5_info->angle_deg_arr[req_slice];
    strcpy(req_rayinfo_name,"startfixangle");
    idx_req=find_in_string_arr(rb5_info->rayinfo_name_arr,rb5_info->n_rayinfos,req_rayinfo_name);
    if(idx_req == -1) {
        for (i = 0; i < this_nrays; i++) {
            (rb5_info->slice_fixed_angle_start_arr[req_slice])[i]=default_val;
            (rb5_info->slice_fixed_angle_start_arr[req_slice])[i]=roundf((rb5_info->slice_fixed_angle_start_arr[req_slice])[i]*precision_factor)/precision_factor;
        }
    } else {
        sprintf(xpath_bgn,"((/volume/scan/slice)[%2d]/slicedata/%s)[%2d]/",req_slice+1,"rayinfo",idx_req+1);
        strRB5_PARAM_INFO rb5_param=get_rb5_param_info(rb5_info,xpath_bgn,L_RB5_PARAM_VERBOSE);
        return_param_blobid_raw(&(*rb5_info), &rb5_param, &raw_arr);
        convert_raw_to_data(&rb5_param,&raw_arr,&data_arr);
        for (i = 0; i < this_nrays; i++) {
            //handle PPI -'ve elevation angles
            if(strcmp(rb5_info->scan_type,"ele") != 0){
                if(data_arr[i] > 270.){
                    data_arr[i]-=360.0;
                }
            }
            (rb5_info->slice_fixed_angle_start_arr[req_slice])[i]=data_arr[i];
            (rb5_info->slice_fixed_angle_start_arr[req_slice])[i]=roundf((rb5_info->slice_fixed_angle_start_arr[req_slice])[i]*precision_factor)/precision_factor;
        } //for (i = 0; i < rb5_param.nrays; i++) {
        if ( raw_arr != NULL ) free( raw_arr);
        if (data_arr != NULL ) free(data_arr);
    }
    
    //fixed_stop_deg_arr
    default_val=rb5_info->angle_deg_arr[req_slice];
    strcpy(req_rayinfo_name,"stopfixangle");
    idx_req=find_in_string_arr(rb5_info->rayinfo_name_arr,rb5_info->n_rayinfos,req_rayinfo_name);
    if(idx_req == -1) {
        for (i = 0; i < this_nrays; i++) {
            (rb5_info->slice_fixed_angle_stop_arr[req_slice])[i]=default_val;
            (rb5_info->slice_fixed_angle_stop_arr[req_slice])[i]=roundf((rb5_info->slice_fixed_angle_stop_arr[req_slice])[i]*precision_factor)/precision_factor;
        }
    } else {
        sprintf(xpath_bgn,"((/volume/scan/slice)[%2d]/slicedata/%s)[%2d]/",req_slice+1,"rayinfo",idx_req+1);
        strRB5_PARAM_INFO rb5_param=get_rb5_param_info(rb5_info,xpath_bgn,L_RB5_PARAM_VERBOSE);
        return_param_blobid_raw(&(*rb5_info), &rb5_param, &raw_arr);
        convert_raw_to_data(&rb5_param,&raw_arr,&data_arr);
        for (i = 0; i < this_nrays; i++) {
            //handle PPI -'ve elevation angles
            if(strcmp(rb5_info->scan_type,"ele") != 0){
                if(data_arr[i] > 270.){
                    data_arr[i]-=360.0;
                }
            }
            (rb5_info->slice_fixed_angle_stop_arr[req_slice])[i]=data_arr[i];
            (rb5_info->slice_fixed_angle_stop_arr[req_slice])[i]=roundf((rb5_info->slice_fixed_angle_stop_arr[req_slice])[i]*precision_factor)/precision_factor;
        } //for (i = 0; i < rb5_param.nrays; i++) {
        if ( raw_arr != NULL ) free( raw_arr);
        if (data_arr != NULL ) free(data_arr);
    }
   
//    fprintf(stdout,"angle_deg_arr[%3d]=%f\n",req_slice,default_val);
    float diff;
    for (i = 0; i < this_nrays; i++) {
        diff=(rb5_info->slice_moving_angle_stop_arr[req_slice])[i]-
             (rb5_info->slice_moving_angle_start_arr[req_slice])[i];
        if((diff < -180) || (diff > 180)) diff=fmodf(diff+360.0,360.0);
        (rb5_info->slice_moving_angle_arr[req_slice])[i]=(rb5_info->slice_moving_angle_start_arr[req_slice])[i]+diff/2.;
//        fprintf(stdout,"[%3d]=%10.5f, %10.5f, %10.5f, %10.5f\n",
//            i,
//            (rb5_info->slice_moving_angle_start_arr[req_slice])[i],
//            (rb5_info->slice_moving_angle_stop_arr[req_slice])[i],
//            diff,
//            (rb5_info->slice_moving_angle_arr[req_slice])[i]);

        diff=(rb5_info->slice_fixed_angle_stop_arr[req_slice])[i]-
             (rb5_info->slice_fixed_angle_start_arr[req_slice])[i];
        if((diff < -180) || (diff > 180)) diff=fmodf(diff+360.0,360.0);
        (rb5_info->slice_fixed_angle_arr[req_slice])[i]=(rb5_info->slice_fixed_angle_start_arr[req_slice])[i]+diff/2.;
//        fprintf(stdout,"[%3d]=%10.5f, %10.5f, %10.5f, %10.5f\n",
//            i,
//            (rb5_info->slice_fixed_angle_start_arr[req_slice])[i],
//            (rb5_info->slice_fixed_angle_stop_arr[req_slice])[i],
//            diff,
//            (rb5_info->slice_fixed_angle_arr[req_slice])[i]);

// handled above in individual start/stop readbacks
//        if(strcmp(rb5_info->scan_type,"ele") == 0){
//            //handle RHI -'ve elevation angles
//            if((rb5_info->slice_moving_angle_arr[req_slice])[i] > 270.)
//               (rb5_info->slice_moving_angle_arr[req_slice])[i]-=360.0;
//        } else {
//            //handle PPI -'ve elevation angles
//            if((rb5_info->slice_fixed_angle_arr[req_slice])[i] > 270.)
//               (rb5_info->slice_fixed_angle_arr[req_slice])[i]-=360.0;
//        }
    }    

}
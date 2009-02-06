#include "db.h"

/* BDB Access method, only 1 should be defined */
// #define BDB_USE_BTREE 
// #define BDB_USE_QUEUE
#define BDB_USE_RECNO 
// #define BDB_USE_HASH 

DB *acc_db; /* Database containing inventory information */
// FILE * errorlog;
int disk_init_permastorage(char* acc_db_name) {
    acc_db = NULL;
    
    u_int32_t open_flags;
    int ret;

    //Removes the previous DB if there
    remove(acc_db_name);

    /* Initialize the DB handle */
    ret = db_create(&acc_db, NULL, 0);
    if (ret != 0) {
        fprintf(stderr, "Create: %s: %s\n", acc_db_name, db_strerror(ret));
        return(ret);
    }

    /* Set up error handling for this database */
    // acc_db->set_errfile(acc_db, stdout);
    // acc_db->set_errpfx(acc_db, "program_name");

    DBTYPE access_method;
#ifdef BDB_USE_BTREE
    access_method = DB_BTREE;
#endif
#ifdef BDB_USE_RECNO
    access_method = DB_RECNO;
#endif
#ifdef BDB_USE_HASH
    access_method = DB_HASH;
#endif
#ifdef BDB_USE_QUEUE
    access_method = DB_QUEUE;
    int max_db_record_size = sizeof(acceptor_record) + PAXOS_MAX_VALUE_SIZE;
    if (PAXOS_MAX_VALUE_SIZE > 8000) {
        printf("Paxos max value size is set too high for using QUEUE\n");
        return(-1);
    }
    ret = acc_db->set_pagesize(acc_db, (4096*2)); 
    if (ret != 0) {
        acc_db->err(acc_db, ret, "Database set pagesize failed.");
        return(ret);
    } 
    ret = acc_db->set_re_len(acc_db, max_db_record_size);
    if (ret != 0) {
        acc_db->err(acc_db, ret, "Database set record size to %d failed.", max_db_record_size);
        return(ret);
    }
    
#endif
    /* Set the open flags */
    open_flags = DB_CREATE;
    
    /* Now open the database */
    ret = acc_db->open(acc_db,        /* Pointer to the database */
                    NULL,       /* Txn pointer */
                    acc_db_name,  /* File name */
                    NULL,       /* Logical db name (unneeded) */
                    access_method,   /* Database type */
                    open_flags, /* Open flags */
                    0);         /* File mode. Using defaults */
    if (ret != 0) {
        acc_db->err(acc_db, ret, "Database '%s' open failed.", acc_db_name);
        return(ret);
    }
printf("ok\n");
                                                                                                                               
    return 0;
}

int disk_force_syncrony() {
    //Force disk flush
    LOG(V_DBG, ("Forcing disk synchrony\n"));
    if(PAXOS_ACCEPTOR_FORCE_DISK_FLUSH) {
        return acc_db->sync(acc_db, 0);        
    } 
    return 0;
}

int disk_cleanup_permastorage() {
    int ret;

    ret =  acc_db->close(acc_db, 0);
    if (ret != 0) {
        fprintf(stderr, "Database close failed: %s\n", db_strerror(ret));
    }
    return ret;
}
int disk_update_record(acceptor_record* rec) {
    DBT key, data;
    memset(&key, 0, sizeof(DBT));
    memset(&data, 0, sizeof(DBT));
    
    //Record + value - value pointer in record
    int size = sizeof(acceptor_record) - sizeof(int*) + rec->value_size;
    // printf("%d + %d = %d\n", (int)sizeof(acceptor_record), (int)rec->value_size, (int)prec_size);

    //Copy data into flatterned record
    acceptor_record * flat_rec = malloc(size);

    flat_rec->iid           = rec->iid;
    flat_rec->ballot        = rec->ballot;
    flat_rec->value_ballot  = rec->value_ballot;
    flat_rec->value_size    = rec->value_size;

    //Copy value directly after structure
    memcpy(&flat_rec->value, rec->value, rec->value_size);

    //key is iid of record
    db_recno_t recno = (rec->iid + 1);
    key.data = &recno;
    key.size = sizeof(db_recno_t);

    //data is flat_rec
    data.data = flat_rec;
    data.size = size;

    int ret = acc_db->put(acc_db, NULL, &key, &data, 0);
    if (ret != 0) {
        fprintf(stderr, "PUT failed: %s\n", db_strerror(ret));
        return -1;
    }
    free(flat_rec);
    
    //Force disk flush
    if(PAXOS_ACCEPTOR_FORCE_DISK_FLUSH)
        acc_db->sync(acc_db, 0);
    return ret;
}

//Return the most recent (based on ballot)
//record info on disk
acceptor_record* disk_lookup_record(int instance_id) {
    int ret;
    acceptor_record* rec;
    DBT key, data;
    
    memset(&key, 0, sizeof(DBT));
    memset(&data, 0, sizeof(DBT));

    //Key is iid + 1
    db_recno_t recno = (instance_id + 1);
    key.data = &recno;
    key.size = sizeof(db_recno_t);

    //Get as flat acceptor record
    ret = acc_db->get(acc_db, NULL, &key, &data, 0);
    if (ret == DB_NOTFOUND || ret == DB_KEYEMPTY) {
        return NULL;
    }
    
    if (ret != 0) {
        fprintf(stderr, "Get failed: %s\n", db_strerror(ret));
        return NULL;
    }
    acceptor_record * flat_rec = (acceptor_record *) data.data;

    //Malloc and copy fields
    rec = malloc(sizeof(acceptor_record));
    rec->iid           = flat_rec->iid;
    rec->ballot        = flat_rec->ballot;
    rec->value_ballot  = flat_rec->value_ballot;
    rec->value_size    = flat_rec->value_size;
    
    //Malloc and copy value
    rec->value = malloc(rec->value_size);
    memcpy(rec->value, &flat_rec->value, rec->value_size);
    
    return rec;
}

//Returns 0 if nothing was found or the 
//largest ballot accepted for the given instance
int disk_lookup_ballot(int instance_id) {
    int ret;
    DBT key, data;
    
    memset(&key, 0, sizeof(DBT));
    memset(&data, 0, sizeof(DBT));

    //Key is iid + 1
    db_recno_t recno = (instance_id + 1);
    key.data = &recno;
    key.size = sizeof(db_recno_t);

    ret = acc_db->get(acc_db, NULL, &key, &data, 0);    
    if (ret == DB_NOTFOUND || ret == DB_KEYEMPTY) {
        return 0;
    }

    if (ret != 0) {
        fprintf(stderr, "Get failed: %s\n", db_strerror(ret));
        return -1;
    }
    
    acceptor_record * flat_rec = (acceptor_record *) data.data;
    
    return flat_rec->ballot;
}


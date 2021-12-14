#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <net/if.h>

#define MAX_HOST_NAME_LENGTH 1024
#define B_TO_KB( x ) ((x) / 1024.0f)
#define B_TO_GB( x ) ((x) / 1024.0f / 1024.0f / 1024.0f)

#define PROC_NET_DEV "/proc/net/dev"
#define PROC_NET_DEVICE_LINE_SSCANF_PATTERN "%63s %63s %*s %*s %*s %*s %*s %*s %63s %63s"

_Static_assert( MAX_HOST_NAME_LENGTH > 1, "Max host name must be positive value > 1" );

// Trim string whitespaces on sides,
// returns new string
char* trim( char* str ) {
    size_t len = strlen( str );

    while ( isspace( str[len - 1] ) ) --len;
    while ( *str && isspace( *str ) ) ++ str, --len;

    return strndup( str, len );
}

// Check is string starts with another string
int is_starts_with( const char* str, const char* prefix ) {
    while ( *prefix ) {
        if ( *prefix++ != *str++ ) {
            return 0;
        }
    }

    return 1;
}

char* file_read_as_string( const char* filename ) {
    FILE* file = NULL;
    size_t length = 0;
    char* ret = NULL;

    if ( (file = fopen( filename, "r" )) == NULL ) return NULL;
    
    fseek( file, 0, SEEK_END );
    length = ftell( file );
    fseek( file, 0, SEEK_SET );

    if ( length == 0 ) goto __exit;

    ret = malloc( length + 1 );

    fread( ret, length, 1, file );

__exit:
    fclose( file );

    return ret;
}

// Most of linux files with information exists in format like:
//    MARK : INFO
//    MARK=INFO
//
// Then, in most situations, everithing we need is just line begins
// with some prefix, for given purposes this function exists
char* get_line_with_prefix_from_file( const char* fname, const char* prefix ) {
    FILE* file = fopen( fname, "rb" );
    assert( file != NULL );

    int bFinded = 0;
    char* line = NULL;
    size_t len = 0;
    while ( getline( &line, &len, file ) != -1 ) {
        if ( is_starts_with( line, prefix ) ) {
            bFinded = 1;
            break;
        }
    }

    fclose( file );

    return bFinded ? line : NULL;
}

// Try to get cpu name from "/proc/cpuinfo" file
char* get_cpu_name() {
    char* line = get_line_with_prefix_from_file( "/proc/cpuinfo", "model name" );

    // First sep returns MARK info, ignore it
    strsep( &line, ":" );

    // Get INFO info, in our situation this is cpu name
    char* token = strsep( &line, ":" );

    // Duplicate cpu name
    char* ret = strdup( token );

    // Trim for pretty output
    char* ret_trimmed = trim( ret );

    // Free temp allocated by getline memory
    free( line );
    free( ret );

    return ret_trimmed;
}

char* get_pretty_release_name() {
    char* line = get_line_with_prefix_from_file( "/etc/os-release", "PRETTY_NAME" );

    // First sep returns MARK info, ignore it
    strsep( &line, "=" );

    // Get INFO info, in our situation this is pretty release name
    char* token = strsep( &line, "=" );

    // Duplicate pretty name
    char* ret = strdup( token );

    // Trim for pretty output
    char* ret_trimmed = trim( ret );

    // Free temp allocated by getline memory
    free( line );
    free( ret );

    return ret_trimmed;
}

int get_net_devices_names( char*** array, int* length ) {
    FILE* file = fopen( PROC_NET_DEV, "r" );
    assert( file != NULL );

    char* line = NULL;
    size_t len = 0;
    size_t idx = 0;
    while ( getline( &line, &len, file ) != -1 ) {
        // Search for pattern like: NAME : PARAMS
        char* dots = strchr( line, ':' );
        if ( dots != NULL ) {
            idx++;
        }
    }

    *array = (char**)malloc( idx * sizeof( char* ) );
    *length = idx;

    idx = 0;

    fseek( file, 0, SEEK_SET );
    while ( getline( &line, &len, file ) != -1 ) {
        char* dots = strchr( line, ':' );
        if ( dots != NULL ) {
            char* sep = strsep( &line, ":" );
            (*array)[idx] = strdup( sep );
            idx++;
        }
    }

    fclose( file );

    return 0;
}

void get_net_device_traffic( const char* device_name, unsigned long* in, unsigned long* out ) {
    char* line = get_line_with_prefix_from_file( PROC_NET_DEV, device_name );
    assert( line != NULL );

    char temp[4][64];

    char* proclineptr = strchr( line, ':' );
    sscanf( proclineptr + 1, PROC_NET_DEVICE_LINE_SSCANF_PATTERN, temp[0], temp[1], temp[2], temp[3] );

    *in = strtoull( temp[0], (char**)NULL, 0 );
    *out = strtoull( temp[2], (char**)NULL, 0 );
}

char* get_motherboad_name() {
    // This files must contains only one string - name of vendor and name of board,
    // so just read and split into one string with space betwen them
    char* vendor = file_read_as_string( "/sys/devices/virtual/dmi/id/board_vendor" );
    char* name = file_read_as_string( "/sys/devices/virtual/dmi/id/board_name" );
    char* ret = NULL;

    if ( vendor == NULL || name == NULL ) return ret;

    // Trim new line symbols
    char* vendor_trimmed = trim( vendor );
    char* name_trimmed = trim( name );

    size_t ret_length = strlen( vendor_trimmed ) + strlen( name_trimmed ) + 2;

    ret = malloc( ret_length );
    memset( ret, 0, ret_length );

    // Concat lines and add space between
    strcat( ret, vendor_trimmed );
    strcat( ret, " " );
    strcat( ret, name_trimmed );

    free( vendor );
    free( name );
    free( vendor_trimmed );
    free( name_trimmed );

    return ret;
}

int main() {
    // Printing values
    char hostname[MAX_HOST_NAME_LENGTH];
    char* motherboard_name = NULL;
    char* cpu_name = NULL;
    unsigned long net_traffic_total_in = 0;
    unsigned long net_traffic_total_out = 0;
    unsigned long memory_avaible = 0;
    unsigned long memory_free = 0;
    char* release_name = NULL;

    // Net temp variables
    char** net_devices = NULL;
    int net_devices_count = 0;
    unsigned long net_traffic_start_in = 0;
    unsigned long net_traffic_start_out = 0;
    unsigned long net_traffic_end_in = 0;
    unsigned long net_traffic_end_out = 0;

    // Get hostname
    gethostname( hostname, MAX_HOST_NAME_LENGTH - 1 );

    // Get motherboard from files in /sys/devices/virtual/dmi/id/board_xxx
    motherboard_name = get_motherboad_name();

    // Get cpu name from /proc/cpuinfo
    cpu_name = get_cpu_name();

    // Calculate start traffic infos
    get_net_devices_names( &net_devices, &net_devices_count );
    for ( int i = 0; i < net_devices_count; i++ ) {
        unsigned long in = 0;
        unsigned long out = 0;
        
        get_net_device_traffic( net_devices[i], &in, &out );

        net_traffic_start_in += in;
        net_traffic_start_out += out;
    }

    // Sleep for 1 second to collect traffic information
    sleep( 1 );

    // Calculate end traffic infos
    for ( int i = 0; i < net_devices_count; i++ ) {
        unsigned long in = 0;
        unsigned long out = 0;
        
        get_net_device_traffic( net_devices[i], &in, &out );

        net_traffic_end_in += in;
        net_traffic_end_out += out;
    }

    // Calculate in/out bytes in this second
    net_traffic_total_in = net_traffic_end_in - net_traffic_start_in;
    net_traffic_total_out = net_traffic_end_out - net_traffic_start_out;

    // Get avaible and free memory information
    struct sysinfo lsysinfo;
    sysinfo( &lsysinfo );

    memory_avaible = lsysinfo.totalram;
    memory_free = lsysinfo.freeram;

    // Get pretty release name from /etc/os-release
    release_name = get_pretty_release_name();

    // Strip '"' araound of name with simple ptr moving and
    // replace to string end symbol
    release_name = strchr( release_name, '"' ) + 1;
    *strchr( release_name, '"' ) = '\0';

    printf( "Server information\n\n" );
    printf( "hostname: %s\n", hostname );
    printf( "motherboard model: %s\n", motherboard_name );
    printf( "cpu model: %s\n", cpu_name );
    printf( "traffic data: in: %.*f Kb/s out: %.*f Kb/s\n", 2, net_traffic_total_in / 1024.0f, 2, net_traffic_total_out / 1024.0f );
    printf( "traffic data: in: %.*f Kb/s out: %.*f Kb/s\n", 2, B_TO_KB( net_traffic_total_in ), 2, B_TO_KB( net_traffic_total_out ) );
    printf( "memory: %.*fG/%.*fG\n", 1, B_TO_GB( memory_free ), 1, B_TO_GB( memory_avaible ) );
    printf( "release: %s\n", release_name );

    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include "liblfds711.h"

struct test_data
{
	struct lfds711_queue_umm_element
	qe;

	int
	number;
};

int main()
{
	int long long unsigned
		loop;

	struct lfds711_queue_umm_element
		*qe,
		qe_dummy;

	struct lfds711_queue_umm_state
		qs;

	struct test_data
		*td,
		*temp_td;

	lfds711_queue_umm_init_valid_on_current_logical_core( &qs, &qe_dummy, NULL );

	td = malloc( sizeof(struct test_data) * 10 );

	// TRD : queue ten elements
	for( loop = 0 ; loop < 10 ; loop++ )
		{
			// TRD : we enqueue the numbers 0 to 9
			td[loop].number = (int) loop;
			LFDS711_QUEUE_UMM_SET_VALUE_IN_ELEMENT( td[loop].qe, &td[loop] );
			lfds711_queue_umm_enqueue( &qs, &td[loop].qe );
		}

	// TRD : dequeue until the queue is empty
	while( lfds711_queue_umm_dequeue(&qs, &qe) )
		{
			temp_td = LFDS711_QUEUE_UMM_GET_VALUE_FROM_ELEMENT( *qe );

			// TRD : we dequeue the numbers 0 to 9
			printf( "number = %d\n", temp_td->number );
		}

	lfds711_queue_umm_cleanup( &qs, NULL );

	free( td );

	return( EXIT_SUCCESS );
}

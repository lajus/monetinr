-- Scenario to exercise the datacell implementation
-- using a single receptor and emitter
-- this is the extended version of scenario00
-- with datacell.threshold option
-- it assumes that events arrive from sensor with a delay of X milliseconds

set optimizer='datacell_pipe';

create table datacell.bakin(
    id integer,
    tag timestamp,
    payload integer
);
create table datacell.bakout( tag timestamp, cnt integer);

call datacell.receptor('datacell.bakin','localhost',50503);

call datacell.emitter('datacell.bakout','localhost',50603);

call datacell.query('datacell.schep', 'insert into datacell.bakout select now(), count(*) from datacell.bakin where datacell.threshold(\'datacell.bakin\',15);');

call datacell.resume();
select * from datacell.receptors(); select * from datacell.emitters(); select * from datacell.queries(); select * from datacell.baskets();

-- externally, activate the sensor 
--sensor --host=localhost --port=50503 --events=100 --columns=3 --delay=1
-- externally, activate the actuator server to listen
-- actuator 


-- wrapup
call datacell.postlude();
drop table datacell.bakin;
drop table datacell.bakout;


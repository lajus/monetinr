SELECT MODEL202.is_mutagen, count(distinct MODEL202.model_id ) FROM MODEL MODEL202, ATOM T1008290413400  WHERE MODEL202.model_id=T1008290413400.model_id AND MODEL202.lumo='-2' group by MODEL202.is_mutagen;
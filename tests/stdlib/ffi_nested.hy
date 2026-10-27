include { std.io, vendors.cnest, }

int main() {
    Outer o; o.p.x = 1.5; o.p.y = 2.0; o.id = 4;
    println("outer: {{n_outer_sum(o)}}");
    Outer om = n_outer_make(3.0, 9);
    println("outer make: {{om.p.x}} {{om.p.y}} {{om.id}}");
    Padded pd; pd.c = 5; pd.d = 2.5;
    println("padded: {{n_padded_sum(pd)}}");
    Padded pm = n_padded_make(7, 0.5);
    println("padded make: {{pm.c}} {{pm.d}}");
    // an array field has no Hylian indexing here: round-trip it through C
    WithArr wm = n_arr_make(5);
    println("arr round trip: {{n_arr_sum(wm)}} {{wm.n}}");
    Tagged tg; tg.tag = 3; tg.u.i = 42;
    println("tagged: {{n_tag_int(tg)}}");
    Quad q; q.a.x = 0; q.a.y = 0; q.a.w = 5; q.a.h = 6; q.b.x = 0; q.b.y = 0; q.b.w = 2; q.b.h = 3;
    println("quad: {{n_quad_area(q)}}");
    Quad qm = n_quad_make(10);
    println("quad make: {{qm.a.x}} {{qm.a.w}} {{qm.a.h}} {{qm.b.x}} {{qm.b.h}}");
    return 0;
}

package gui

import "testing"

func TestChildArgsRemoveGui(t *testing.T) {
	got := ChildArgs([]string{"wydserver", "--gui", "-admin", "127.0.0.1:7480", "--cli"})
	want := []string{"wydserver", "-admin", "127.0.0.1:7480", "--cli"}
	if len(got) != len(want) {
		t.Fatalf("ChildArgs = %v; want %v", got, want)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("ChildArgs[%d] = %q; want %q", i, got[i], want[i])
		}
	}
}

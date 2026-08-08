import { describe, expect, test } from "bun:test";
import {
  copyWidgetCode,
  createWidgetCode,
  getOwnedActiveSites,
} from "./widgets.js";

describe("widget code", () => {
  test("creates the small terminal widget", () => {
    const code = createWidgetCode(
      "https://ring.example/path",
      "my-site",
      "small",
    );

    expect(code).toContain('data-r="https://ring.example"');
    expect(code).toContain('data-k="my-site"');
    expect(code).toContain(
      'src:url("https://ring.example/font/woff2/terminus.woff2")',
    );
    expect(code).toContain("font:14px Terminus,monospace");
    expect(code).toContain('data-s="previous"');
    expect(code).toContain('data-s="next"');
    expect(code).toContain("width:60px;height:135px");
    expect(code).toContain("font:14px/1.2");
    expect(code).toContain("font:inherit");
    expect(code).toContain(">↑</a>");
    expect(code).toContain("<b>bksp</b>");
    expect(code).toContain(">↓</a>");
    expect(code).toContain("transform:rotate(90deg)");
    expect(code).toContain("margin:0");
    expect(code).toContain("<style>");
    expect(code).toContain("<script>");
    expect(code).not.toContain("random");
    expect(code).not.toContain("fetch(");
    expect(code).not.toContain("iframe");
    expect(code).not.toContain("\n");
  });

  test("creates the long horizontal widget", () => {
    const code = createWidgetCode("https://ring.example", "long-site", "long");

    expect(code).toContain('class="wr h"');
    expect(code).toContain("max-width:560px;min-height:44px");
    expect(code).toContain("←---");
    expect(code).toContain("b4cksp4ce webring");
    expect(code).toContain("---→");
    expect(code).toContain('data-s="previous"');
    expect(code).toContain('data-s="next"');
  });

  test("rejects an unknown variant", () => {
    expect(() =>
      createWidgetCode("https://ring.example", "site", "wide"),
    ).toThrow("Unknown widget variant");
  });
});

describe("owned active sites", () => {
  const sites = [
    { slug: "mine", owner_oauth: "github", owner_tag: "wolf" },
    { slug: "theirs", owner_oauth: "github", owner_tag: "rabbit" },
    { slug: "same-tag", owner_oauth: "telegram", owner_tag: "wolf" },
  ];

  test("matches both the provider and handle", () => {
    expect(
      getOwnedActiveSites(sites, {
        oauth: "github",
        tag: "wolf",
        is_admin: true,
      }),
    ).toEqual([sites[0]]);
  });

  test("returns no sites without a signed-in account", () => {
    expect(getOwnedActiveSites(sites, null)).toEqual([]);
  });
});

describe("widget copying", () => {
  test("writes the complete code", async () => {
    let copiedCode = null;
    const clipboard = {
      writeText(code) {
        copiedCode = code;
        return Promise.resolve();
      },
    };

    await copyWidgetCode(clipboard, "widget code");

    expect(copiedCode).toBe("widget code");
  });

  test("passes through a rejected write", async () => {
    const clipboard = {
      writeText() {
        return Promise.reject(new Error("denied"));
      },
    };

    await expect(copyWidgetCode(clipboard, "widget code")).rejects.toThrow(
      "denied",
    );
  });

  test("rejects an unavailable clipboard", async () => {
    await expect(copyWidgetCode(null, "widget code")).rejects.toThrow(
      "Clipboard access is unavailable",
    );
  });
});

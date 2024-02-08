// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
import { RgbaColor } from "./color"
import { compareVersions } from "./signature";
import { Widget, WidgetType, GenerateKey } from './widget'
import { makeDefaultWidgetMetric } from "./widget-metric";
import { QualifiedMetric } from "./qualified-metric";

export interface Graph extends Widget {
    height: number;
    vDivs: number;
    hDivs: number;
    showBottomAxis: boolean;
    graphType: {
        name: string,
        range: [number, number],
        rangeRight: [number, number],
        binCount: number,
        countRange: [number, number],
        autoLeft: boolean,
        autoRight: boolean,
        autoCount: boolean,
    },
    gridColor: RgbaColor;
    dividerColor: RgbaColor;
    backgroundColor: RgbaColor;
    borderColor: RgbaColor;
    textColor: RgbaColor;
    textSize: number;
}

export function makeDefaultGraph(metric: QualifiedMetric|null = null): Graph {
    return {
        key: GenerateKey(),
        metrics: [makeDefaultWidgetMetric(metric)],
        widgetType: WidgetType.Graph,
        height: 80,
        vDivs: 4,
        hDivs: 40,
        showBottomAxis: false,
        graphType: {
          name: 'Line',
          range: [0, 150],
          rangeRight: [0, 150],
          binCount: 40,
          countRange: [0, 1000],
          autoLeft: true,
          autoRight: true,
          autoCount: false,
        },
        gridColor: {
          r: 47,
          g: 120,
          b: 190,
          a: 40 / 255,
        },
        dividerColor: {
          r: 57,
          g: 126,
          b: 150,
          a: 220 / 255,
        },
        backgroundColor: {
          r: 0,
          g: 0,
          b: 0,
          a: 0,
        },
        borderColor: {
          r: 0,
          g: 0,
          b: 0,
          a: 0,
        },
        textColor: { 
            r: 242, 
            g: 242, 
            b: 242, 
            a: 1.0, 
        },        
        textSize: 11,
    };
}


interface Migration {
  version: string;
  migrate: (graph: Graph) => void;
}

const migrations: Migration[] = [
  {
      version: '0.13.0',
      migrate: (graph: Graph) => {
          let e = new Error('Cannot migrate loadouts below version 0.13.0');
          (e as any).noticeOverride = true;
          throw e;
      }
  },
];

migrations.sort((a, b) => compareVersions(a.version, b.version));

export function migrateGraph(graph: Graph, sourceVersion: string): void {
  for (const mig of migrations) {
      if (compareVersions(mig.version, sourceVersion) > 0) {
          mig.migrate(graph);
      }
  }
}